"""Transport differences for the standalone ATH Gateway supported by this integration."""

from __future__ import annotations

from collections.abc import Awaitable
from typing import Any, TypeVar

import httpx
from ath import AsyncATHGatewayClient
from ath.client._base import require_token
from ath.exceptions import ATHError as SDKError
from ath.types import AgentStatus, ProviderApproval
from joserfc.errors import JoseError
from pydantic import BaseModel, ValidationError

from .errors import ATHError
from .models import ATHConfig, GatewaySession

T = TypeVar("T")


class AgentStatusResponse(BaseModel):
    client_id: str
    agent_id: str
    agent_status: AgentStatus
    approved_providers: list[ProviderApproval]
    approval_expires: str


class GatewayTransport(AsyncATHGatewayClient):
    def __init__(self, config: ATHConfig, session: GatewaySession) -> None:
        super().__init__(
            config.gateway_url, config.agent_id, config.private_key,
            key_id=config.key_id, timeout=config.timeout,
        )
        # ath-sdk 0.1.0 has no public HTTP client or per-request header injection.
        # Keep all access to its internal HTTP client here.
        self._gateway_http = self._http
        origin = httpx.URL(config.gateway_url)

        async def add_session_header(request: httpx.Request) -> None:
            url = request.url
            if (url.scheme, url.host, url.port) != (origin.scheme, origin.host, origin.port):
                raise ATHError("unexpected_gateway_origin", "Unexpected Gateway request destination")
            if url.path != "/.well-known/ath.json":
                request.headers["X-ATH-User-Token"] = session.token

        self._gateway_http.event_hooks["request"].append(add_session_header)

    async def _get_gateway_json(self, path: str) -> Any:
        response = await self._gateway_http.get(f"{self.url}{path}")
        payload = response.json()
        if not response.is_success:
            fields = payload if isinstance(payload, dict) else {}
            raise SDKError(str(fields.get("code")), str(fields.get("message")), status=response.status_code)
        return payload

    async def verify_gateway_user(self, user_id: str) -> None:
        payload = await self._get_gateway_json("/auth/me")
        if not isinstance(payload, dict) or payload.get("id") != user_id:
            raise ATHError("gateway_user_mismatch", "Gateway login does not match the configured user")

    async def registration_status(self, client_id: str) -> AgentStatusResponse:
        return AgentStatusResponse.model_validate(await self._get_gateway_json(f"/ath/agents/{client_id}"))

    async def proxy(self, provider: str, method: str, path: str, body: Any = None) -> Any:
        if method.upper() != "HEAD":
            return await super().proxy(provider, method, path, body)

        token = require_token(self.access_token)
        if not path.startswith("/"):
            path = f"/{path}"
        await self._raw_request(
            "HEAD",
            self._ath_url(f"/ath/proxy/{provider}{path}"),
            body,
            {"Authorization": f"Bearer {token}", "X-ATH-Agent-ID": self.agent_id},
        )
        return None


async def gateway_call(request: Awaitable[T], *, phase: str = "") -> T:
    try:
        return await request
    except SDKError as error:
        mapped = _gateway_error(error, phase)
        raise mapped from None
    except httpx.RequestError:
        raise ATHError("transport_error", "Could not complete the request to the ATH Gateway") from None
    except JoseError:
        raise ATHError("invalid_agent_key", "Could not sign the ATH Agent attestation") from None
    except (ValidationError, ValueError):
        raise ATHError("invalid_gateway_response", "Invalid response from the ATH Gateway") from None


def _gateway_error(error: SDKError, phase: str) -> ATHError:
    code = error.code
    message = str(error)
    if phase == "exchange":
        if code == "SESSION_NOT_FOUND" and message == "Authorization not yet completed":
            return ATHError("interaction_required", "Complete authorization in the browser", http_status=error.status)
        if code in {"SESSION_EXPIRED", "SESSION_NOT_FOUND"} or (
            code == "INTERNAL_ERROR" and message == "Session expired"
        ):
            return ATHError("authorization_expired", "The ATH authorization transaction has expired", http_status=error.status)
        if code == "USER_DENIED" or (
            code == "INTERNAL_ERROR" and message in {"User denied consent", "User denied the authorization request"}
        ):
            return ATHError("authorization_denied", "The user denied ATH authorization", http_status=error.status)
        if code in {"INTERNAL_ERROR", "OAUTH_ERROR"} and error.status == 400:
            return ATHError("authorization_failed", "The provider could not complete authorization", http_status=error.status)

    codes = {
        "AUTH_REQUIRED": ("gateway_session_required", "A valid Gateway login session is required"),
        "AGENT_NOT_REGISTERED": ("agent_not_registered", "The ATH Agent registration is missing or invalid"),
        "AGENT_UNAPPROVED": ("agent_unapproved", "The Gateway has not approved the Agent registration"),
        "CONFLICT": (
            "registration_credentials_missing",
            "The Agent is already registered for this Gateway user. Restore its ATH registration credentials",
        ),
        "FORBIDDEN": ("insufficient_scope", "The Gateway refused this request"),
        "PROVIDER_NOT_APPROVED": ("insufficient_scope", "The ATH provider has not been approved"),
        "SCOPE_NOT_APPROVED": ("insufficient_scope", "The required ATH scopes have not been approved"),
        "TOKEN_EXPIRED": ("token_expired", "The ATH access token has expired"),
        "TOKEN_INVALID": ("token_invalid", "The Gateway rejected the ATH access token"),
        "TOKEN_REVOKED": ("token_revoked", "The ATH access token has been revoked"),
        "INVALID_ATTESTATION": ("invalid_attestation", "The Gateway rejected the ATH Agent attestation"),
        "INVALID_RESPONSE": ("invalid_gateway_response", "Invalid response from the ATH Gateway"),
    }
    if phase == "proxy" and code == "UNKNOWN":
        mapped = ("service_error", "The service could not complete the proxied request")
    else:
        mapped = codes.get(code, ("gateway_error", "The ATH Gateway rejected the request"))
    return ATHError(*mapped, http_status=error.status)
