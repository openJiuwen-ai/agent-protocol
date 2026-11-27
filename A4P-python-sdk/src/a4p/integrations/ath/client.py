from __future__ import annotations

import asyncio
import re
import secrets
from datetime import datetime, timedelta
from typing import TYPE_CHECKING, Any

from .credentials import (
    AccessGrant,
    MemoryRegistrationStore,
    PendingRevocation,
    RegistrationCredentials,
    RegistrationKey,
    RegistrationStore,
)
from .errors import ATHError
from .models import (
    ATHConfig,
    AuthorizationRequired,
    AuthorizationResult,
    Authorized,
    GatewaySession,
    Registration,
    _http_url,
)
from .sessions import AuthorizationSessions, AuthorizationTransaction, utcnow

if TYPE_CHECKING:
    from ath.types import AgentRegistrationResponse, DiscoveryDocument

    from ._gateway import AgentStatusResponse


_INVALID_TOKENS = {"token_expired", "token_invalid", "token_revoked"}


class ATHClient:
    """Asynchronous facade for one Gateway login, Agent and provider.

    Supply application session IDs from the authenticated host, never from model input.
    Transactions and access tokens are isolated by application session and live in memory.
    """

    def __init__(
        self,
        config: ATHConfig,
        gateway_session: GatewaySession,
        *,
        registration_store: RegistrationStore | None = None,
    ) -> None:
        try:
            from ._gateway import GatewayTransport, gateway_call
        except ModuleNotFoundError as error:
            if error.name == "ath":
                raise ImportError("ATH support requires the a4p[ath] extra") from None
            raise

        self._config = config
        self._registration_key = RegistrationKey(config.gateway_url, gateway_session.user_id, config.agent_id)
        self._registration_store = (
            registration_store if registration_store is not None else MemoryRegistrationStore()
        )
        self._gateway = GatewayTransport(config, gateway_session)
        self._gateway_call = gateway_call
        self._user_verified = False
        self._unsaved_registration: RegistrationCredentials | None = None
        self._transactions = AuthorizationSessions()
        self._grants: dict[str, AccessGrant] = {}
        self._pending_revocations: dict[str, PendingRevocation] = {}
        self._next_grant_expiry: datetime | None = None
        self._lock = asyncio.Lock()
        self._closed = False

    async def __aenter__(self) -> ATHClient:
        self._check_open()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.aclose()

    async def discover(self) -> DiscoveryDocument:
        async with self._lock:
            self._check_open()
            return await self._gateway_call(self._gateway.discover())

    async def ensure_registered(self) -> Registration:
        async with self._lock:
            await self._verify_gateway_user()
            return await self._ensure_registered()

    async def begin_authorization(self, application_session: str) -> AuthorizationResult:
        """Return an active grant or open one consent transaction for this application session."""
        self._require_session(application_session)
        async with self._lock:
            await self._verify_gateway_user()
            await self._revoke_partial_grant(application_session)
            grant = self._grants.get(application_session)
            if grant and grant.authorization.expires_at > utcnow():
                return grant.authorization
            self._grants.pop(application_session, None)

            pending = self._transactions.for_session(application_session)
            if pending:
                return pending.interaction

            registration = await self._ensure_registered()
            expires_at = utcnow() + timedelta(seconds=self._config.authorization_ttl)
            response = await self._gateway_call(self._gateway.authorize(
                self._config.provider_id,
                sorted(self._config.scopes),
                redirect_uri=self._redirect_uri,
            ))
            if not response.ath_session_id or not _http_url(
                response.authorization_url, allow_http=self._config.allow_insecure_http
            ):
                raise ATHError("invalid_gateway_response", "The Gateway did not return a valid authorization entry")
            interaction = AuthorizationRequired(
                secrets.token_urlsafe(24), response.authorization_url, expires_at,
            )
            self._transactions.add(AuthorizationTransaction(
                interaction, application_session, response.ath_session_id, registration.client_id,
            ))
            return interaction

    async def resume_authorization(
        self, application_session: str, transaction_id: str,
    ) -> AuthorizationResult:
        """Ask the Gateway to exchange its completed OAuth session exactly once.

        A pending response leaves the transaction available for the user's next
        explicit continuation. A request with an uncertain exchange outcome ends
        the local transaction. If this raises `revocation_pending`, retry with
        `revoke(application_session)` before closing this client.
        """
        self._require_session(application_session)
        async with self._lock:
            await self._verify_gateway_user()
            transaction = self._transactions.get(transaction_id)
            if transaction is None or transaction.application_session != application_session:
                raise ATHError("invalid_transaction", "The ATH authorization transaction is unavailable")
            if transaction.interaction.expires_at <= utcnow():
                self._transactions.discard(transaction_id)
                raise ATHError("authorization_expired", "The ATH authorization transaction has expired")

            credentials = await self._registration_store.get(self._registration_key)
            if credentials is None or credentials.client_id != transaction.registration_id:
                self._transactions.discard(transaction_id)
                raise ATHError("agent_not_registered", "The ATH Agent registration has changed")
            self._set_credentials(credentials)
            issued_at = utcnow()
            try:
                # Standalone Gateway uses its callback state and ignores the SDK's required code field.
                response = await self._gateway_call(
                    self._gateway.exchange_token("exchange", transaction.gateway_session_id),
                    phase="exchange",
                )
            except ATHError as error:
                if error.code == "interaction_required":
                    return transaction.interaction
                if error.code != "gateway_session_required":
                    self._transactions.discard(transaction_id)
                raise
            except asyncio.CancelledError:
                self._transactions.discard(transaction_id)
                raise
            finally:
                self._gateway.set_token("")

            self._transactions.discard(transaction_id)
            scopes = frozenset(response.effective_scopes)
            if (
                response.agent_id != self._config.agent_id
                or response.provider_id != self._config.provider_id
                or not re.fullmatch(r"[!-~]+", response.access_token)
                or response.expires_in <= 0
            ):
                raise ATHError("invalid_gateway_response", "The Gateway returned an invalid ATH token binding")
            if not self._config.scopes <= scopes:
                self._pending_revocations[application_session] = PendingRevocation(
                    credentials, response.access_token,
                )
                await self._revoke_partial_grant(application_session)
                raise ATHError("insufficient_scope", "User consent did not grant all required ATH scopes")
            try:
                expires_at = issued_at + timedelta(seconds=response.expires_in)
            except OverflowError:
                raise ATHError("invalid_gateway_response", "Invalid ATH token expiry") from None
            authorization = Authorized(scopes, expires_at)
            self._store_grant(application_session, AccessGrant(authorization, response.access_token))
            return authorization

    async def cancel_authorization(self, application_session: str, transaction_id: str) -> None:
        """Cancel a local pending transaction so that it can no longer be exchanged."""
        self._require_session(application_session)
        async with self._lock:
            self._check_open()
            transaction = self._transactions.get(transaction_id)
            if transaction is None or transaction.application_session != application_session:
                raise ATHError("invalid_transaction", "The ATH authorization transaction is unavailable")
            self._transactions.discard(transaction_id)

    async def call_api(self, application_session: str, operation: str, *, body: Any = None) -> Any:
        """Call the configured operation once through the official SDK."""
        self._require_session(application_session)
        endpoint = self._config.operations.get(operation)
        if endpoint is None:
            raise ValueError("Unknown ATH operation")
        async with self._lock:
            await self._verify_gateway_user()
            grant = self._grants.get(application_session)
            if grant is None:
                raise ATHError("authorization_required", "Complete ATH authorization before calling the service")
            if grant.authorization.expires_at <= utcnow():
                self._grants.pop(application_session)
                raise ATHError("token_expired", "The ATH access token has expired")
            if not endpoint.scopes <= grant.authorization.scopes:
                raise ATHError("insufficient_scope", "The ATH token does not cover the requested operation")
            self._gateway.set_token(grant.token)
            try:
                return await self._gateway_call(
                    self._gateway.proxy(self._config.provider_id, endpoint.method, endpoint.path, body),
                    phase="proxy",
                )
            except ATHError as error:
                if error.code in _INVALID_TOKENS:
                    self._grants.pop(application_session)
                raise
            finally:
                self._gateway.set_token("")

    async def revoke(self, application_session: str) -> None:
        """Revoke the session's ATH token, including a partial grant awaiting cleanup."""
        self._require_session(application_session)
        async with self._lock:
            await self._verify_gateway_user()
            pending = self._transactions.for_session(application_session)
            if pending:
                self._transactions.discard(pending.interaction.transaction_id)
            await self._revoke_partial_grant(application_session)
            grant = self._grants.get(application_session)
            if grant is None:
                return
            credentials = await self._registration_store.get(self._registration_key)
            if credentials is None:
                raise ATHError("agent_not_registered", "ATH registration credentials are required for revocation")
            await self._revoke_token(credentials, grant.token)
            self._grants.pop(application_session)

    async def _revoke_partial_grant(self, application_session: str) -> None:
        pending = self._pending_revocations.get(application_session)
        if pending is None:
            return
        try:
            await self._revoke_token(pending.credentials, pending.token)
        except ATHError as error:
            raise ATHError(
                "revocation_pending",
                f"Consent did not grant all required ATH scopes and token revocation failed ({error.code})",
                http_status=error.http_status,
            ) from None
        self._pending_revocations.pop(application_session)

    async def _revoke_token(self, credentials: RegistrationCredentials, token: str) -> None:
        self._set_credentials(credentials)
        self._gateway.set_token(token)
        try:
            await self._gateway_call(self._gateway.revoke())
        except ATHError as error:
            if error.code not in _INVALID_TOKENS:
                raise
        finally:
            self._gateway.set_token("")

    async def aclose(self) -> None:
        async with self._lock:
            if self._closed:
                return
            self._closed = True
            self._transactions.clear()
            self._grants.clear()
            self._pending_revocations.clear()
            self._unsaved_registration = None
            await self._gateway.close()

    @property
    def _redirect_uri(self) -> str:
        return f"{self._config.gateway_url}/ui/callback"

    async def _verify_gateway_user(self) -> None:
        self._check_open()
        if not self._user_verified:
            await self._gateway_call(self._gateway.verify_gateway_user(self._registration_key.gateway_user_id))
            self._user_verified = True

    async def _ensure_registered(self) -> Registration:
        if self._unsaved_registration is not None:
            await self._save_registration()
        credentials = await self._registration_store.get(self._registration_key)
        if credentials:
            if not re.fullmatch(r"[A-Za-z0-9_-]+", credentials.client_id):
                raise ATHError("invalid_registration", "Invalid stored ATH client ID")
            try:
                response = await self._gateway_call(self._gateway.registration_status(credentials.client_id))
            except ATHError as error:
                if error.code != "agent_not_registered" or error.http_status != 404:
                    raise
            else:
                if response.agent_id != self._config.agent_id or response.client_id != credentials.client_id:
                    raise ATHError("invalid_registration", "Gateway registration does not match the configured Agent")
                self._set_credentials(credentials)
                return self._check_registration(response)

        response = await self._gateway_call(self._gateway.register(
            developer={"name": self._config.developer_name, "id": self._config.developer_id},
            providers=[{"provider_id": self._config.provider_id, "scopes": sorted(self._config.scopes)}],
            purpose=self._config.purpose,
            redirect_uris=[self._redirect_uri],
        ))
        if not re.fullmatch(r"[A-Za-z0-9_-]+", response.client_id) or not response.client_secret:
            raise ATHError("invalid_gateway_response", "The Gateway did not return valid registration credentials")
        self._unsaved_registration = RegistrationCredentials(response.client_id, response.client_secret)
        await self._save_registration()
        return self._check_registration(response)

    async def _save_registration(self) -> None:
        credentials = self._unsaved_registration
        if credentials is not None:
            await self._registration_store.put(self._registration_key, credentials)
            self._unsaved_registration = None

    def _check_registration(self, response: AgentRegistrationResponse | AgentStatusResponse) -> Registration:
        if response.agent_status == "pending":
            raise ATHError("registration_pending", "The ATH Agent registration is awaiting approval")
        if response.agent_status == "denied":
            raise ATHError("registration_denied", "The Gateway denied the ATH Agent registration")
        try:
            expires_at = datetime.fromisoformat(response.approval_expires.replace("Z", "+00:00"))
        except ValueError:
            raise ATHError("invalid_gateway_response", "Invalid ATH registration expiry") from None
        if expires_at.tzinfo is None:
            raise ATHError("invalid_gateway_response", "Invalid ATH registration expiry")
        if expires_at <= utcnow():
            raise ATHError("registration_expired", "The ATH Agent registration has expired")
        scopes = frozenset(
            scope for provider in response.approved_providers
            if provider.provider_id == self._config.provider_id
            for scope in provider.approved_scopes
        )
        if not self._config.scopes <= scopes:
            raise ATHError("insufficient_scope", "The registration does not cover the required ATH provider scopes")
        return Registration(response.client_id, scopes, expires_at)

    def _store_grant(self, application_session: str, grant: AccessGrant) -> None:
        now = utcnow()
        if self._next_grant_expiry is not None and self._next_grant_expiry <= now:
            self._grants = {
                session: stored for session, stored in self._grants.items()
                if stored.authorization.expires_at > now
            }
            self._next_grant_expiry = min(
                (stored.authorization.expires_at for stored in self._grants.values()), default=None,
            )
        self._grants[application_session] = grant
        if self._next_grant_expiry is None or grant.authorization.expires_at < self._next_grant_expiry:
            self._next_grant_expiry = grant.authorization.expires_at

    def _set_credentials(self, credentials: RegistrationCredentials) -> None:
        self._gateway.set_credentials(credentials.client_id, credentials.client_secret)

    def _check_open(self) -> None:
        if self._closed:
            raise ATHError("client_closed", "The ATH client is closed")

    @staticmethod
    def _require_session(application_session: str) -> None:
        if not isinstance(application_session, str) or not application_session.strip():
            raise ValueError("A trusted application session ID is required")
