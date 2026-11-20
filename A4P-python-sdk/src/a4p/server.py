"""Public A4P Server facade."""

from __future__ import annotations

from dataclasses import asdict

from a4p.user_signature.webauthn import (
    WEBAUTHN_SIGNATURE_METHOD,
    WebAuthnSignatureMethod,
)
from a4p.user_signature.ed25519 import (
    ED25519_SIGNATURE_METHOD,
    RegisteredEd25519Method,
)
from a4p.errors import SignatureMethodNotEnabledError
from a4p.intent.mandate import IntentDisplayTextRenderer
from a4p.intent.service import IntentAuthorizationService
from a4p.intent.signing import intent_server_trusted_key
from a4p.intent.usage_store import A4PIntentTokenUsageStore, SQLiteIntentTokenUsageStore
from a4p.operation.mandate import OperationDisplayTextRenderer
from a4p.operation.service import OperationAuthorizationService
from a4p.operation.signing import operation_server_trusted_key
from a4p.types import (
    IntentAuthorizationRequest,
    IntentAuthorizationResponse,
    OperationAuthorizationChallenge,
    OperationAuthorizationCompletionRequest,
    OperationAuthorizationRequest,
    OperationAuthorizationResult,
    TokenVerificationRequest,
    TokenVerificationResponse,
)
from a4p.user_signature import A4PUserSignatureMethod


class A4PServer:
    """Facade configured with exactly one user-signature method."""

    def __init__(
        self,
        *,
        server_id: str = "local://a4p",
        user_signature_method: A4PUserSignatureMethod | None = None,
        intent_display_text_renderer: IntentDisplayTextRenderer | None = None,
        operation_display_text_renderer: OperationDisplayTextRenderer | None = None,
        require_user_signature: bool = True,
        intent_token_usage_store: A4PIntentTokenUsageStore | None = None,
    ) -> None:
        if require_user_signature and user_signature_method is None:
            raise ValueError(
                "user_signature_method is required when user signatures are enabled"
            )
        if user_signature_method is not None:
            method = user_signature_method.signature_method
            if not method or method != method.strip().lower():
                raise ValueError(
                    "user_signature_method.signature_method must be a non-empty "
                    "lowercase identifier"
                )
        self.server_id = server_id
        self.user_signature_method = user_signature_method
        self.intent_display_text_renderer = intent_display_text_renderer
        self.operation_display_text_renderer = operation_display_text_renderer
        self.require_user_signature = require_user_signature
        self.intent_token_usage_store = (
            intent_token_usage_store
            if intent_token_usage_store is not None
            else SQLiteIntentTokenUsageStore()
        )
        self._intent = IntentAuthorizationService(
            server_id=self.server_id,
            display_text_renderer=self.intent_display_text_renderer,
            require_user_signature=self.require_user_signature,
            user_signature_method=self.user_signature_method,
            token_usage_store=self.intent_token_usage_store,
        )
        self._operation = OperationAuthorizationService(
            server_id=self.server_id,
            display_text_renderer=self.operation_display_text_renderer,
            require_user_signature=self.require_user_signature,
            user_signature_method=self.user_signature_method,
        )

    @property
    def signature_method(self) -> str | None:
        return (
            self.user_signature_method.signature_method
            if self.user_signature_method is not None
            else None
        )

    def _require_signature_method(self, requested: str) -> None:
        if self.signature_method != requested:
            raise SignatureMethodNotEnabledError(
                expected=self.signature_method,
                requested=requested,
            )

    def server_trust_config(self) -> dict[str, dict[str, dict[str, str]]]:
        intent_key = intent_server_trusted_key()
        operation_key = operation_server_trusted_key()
        return {
            self.server_id: {
                intent_key["keyId"]: {
                    "alg": intent_key["alg"],
                    "publicKey": intent_key["publicKey"],
                },
                operation_key["keyId"]: {
                    "alg": operation_key["alg"],
                    "publicKey": operation_key["publicKey"],
                },
            }
        }

    def register_ed25519_credential(self, request: dict) -> dict:
        self._require_signature_method(ED25519_SIGNATURE_METHOD)
        method = self.user_signature_method
        if not isinstance(method, RegisteredEd25519Method):
            raise ValueError("Configured ed25519 method does not support registration")
        return method.register(request)

    def webauthn_registration_options(self, request: dict) -> dict:
        self._require_signature_method(WEBAUTHN_SIGNATURE_METHOD)
        method = self.user_signature_method
        if not isinstance(method, WebAuthnSignatureMethod):
            raise ValueError("Configured webauthn method does not support registration")
        user_id = str(request.get("userId") or "").strip()
        if not user_id:
            raise ValueError("userId missing")
        return method.registration_options(
            user_id=user_id,
            user_name=str(request.get("userName") or user_id),
            user_display_name=str(
                request.get("userDisplayName") or request.get("userName") or user_id
            ),
        )

    def verify_webauthn_registration(self, request: dict) -> dict:
        self._require_signature_method(WEBAUTHN_SIGNATURE_METHOD)
        method = self.user_signature_method
        if not isinstance(method, WebAuthnSignatureMethod):
            raise ValueError("Configured webauthn method does not support registration")
        record = method.verify_registration(request)
        return {
            "registered": True,
            "created": True,
            "credential": asdict(record),
        }

    async def prepare_intent_authorization(
        self,
        request: IntentAuthorizationRequest | dict,
    ) -> IntentAuthorizationResponse:
        return await self._intent.prepare(request)

    async def complete_intent_authorization(
        self,
        request: dict,
    ) -> IntentAuthorizationResponse:
        return await self._intent.complete(request)

    async def verify_intent_token(
        self,
        request: TokenVerificationRequest | dict,
    ) -> TokenVerificationResponse:
        return await self._intent.verify_token(request)

    async def prepare_operation_authorization(
        self,
        request: OperationAuthorizationRequest | dict,
    ) -> OperationAuthorizationChallenge:
        return await self._operation.prepare(request)

    async def complete_operation_authorization(
        self,
        request: OperationAuthorizationCompletionRequest | dict,
    ) -> OperationAuthorizationResult:
        return await self._operation.complete(request)


__all__ = ["A4PServer"]
