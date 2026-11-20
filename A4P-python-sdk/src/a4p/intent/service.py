"""A4P Server orchestration for intent authorization."""

from __future__ import annotations

import calendar
import time
from typing import Any

from a4p.authorization_common import error_code, mandate_matches_pending, payload
from a4p.errors import UserCredentialNotRegisteredError
from a4p.intent.mandate import (
    IntentDisplayTextRenderer,
    create_intent_mandate,
    normalize_intent_mandate,
    normalize_intent_scope,
    verify_intent_mandate,
)
from a4p.intent.token import issue_intent_token, verify_intent_token
from a4p.intent.usage_store import A4PIntentTokenUsageStore, IntentTokenUsageStoreError
from a4p.mandate_security import mandate_identifier
from a4p.types import (
    IntentAuthorizationRequest,
    IntentAuthorizationResponse,
    TokenVerificationRequest,
    TokenVerificationResponse,
    VerificationResult,
)
from a4p.user_signature import A4PUserSignatureMethod


class IntentAuthorizationService:
    """Prepare and complete intent mandates, then verify issued tokens."""

    def __init__(
        self,
        *,
        server_id: str,
        display_text_renderer: IntentDisplayTextRenderer | None,
        require_user_signature: bool,
        user_signature_method: A4PUserSignatureMethod | None,
        token_usage_store: A4PIntentTokenUsageStore,
    ) -> None:
        self.server_id = server_id
        self.display_text_renderer = display_text_renderer
        self.require_user_signature = require_user_signature
        self.user_signature_method = user_signature_method
        self.token_usage_store = token_usage_store
        self._pending: dict[str, dict[str, Any]] = {}

    async def prepare(
        self,
        request: IntentAuthorizationRequest | dict[str, Any],
    ) -> IntentAuthorizationResponse:
        request_payload = payload(request)
        intent = request_payload.get("intent") if isinstance(request_payload.get("intent"), dict) else {}
        try:
            agent_id = str(request_payload.get("agentId") or "").strip()
            if not agent_id:
                raise ValueError("agentId missing")
            user_id = str(request_payload.get("userId") or "").strip()
            if not user_id:
                raise ValueError("userId missing")
            validity_raw = request_payload.get("validitySeconds")
            if validity_raw is None:
                validity_seconds = 3600
            elif isinstance(validity_raw, bool) or not isinstance(validity_raw, int) or validity_raw <= 0:
                raise ValueError("validitySeconds must be a positive integer")
            else:
                validity_seconds = validity_raw
            if "executionPolicy" in intent and intent.get("executionPolicy") is None:
                raise ValueError("executionPolicy must include maxExecutions")
            mandate = create_intent_mandate(
                server=str(request_payload.get("server") or self.server_id),
                agent_id=agent_id,
                actions=intent.get("actions"),
                execution_policy=intent.get("executionPolicy") if "executionPolicy" in intent else None,
                validity_seconds=validity_seconds,
                agent_public_key=request_payload.get("agentPublicKey") if isinstance(request_payload.get("agentPublicKey"), dict) else None,
                require_user_signature=self.require_user_signature,
                user_signature_method=(
                    self.user_signature_method.signature_method
                    if self.user_signature_method is not None
                    else None
                ),
                user_signature_method_policy=(
                    self.user_signature_method.method_policy()
                    if self.user_signature_method is not None
                    else None
                ),
                display_text_renderer=self.display_text_renderer,
            )
            mandate_id = mandate_identifier(mandate)
            signing_options = (
                self.user_signature_method.signing_options(
                    user_id=user_id,
                    mandate=mandate,
                )
                if self.require_user_signature and self.user_signature_method is not None
                else {}
            )
        except UserCredentialNotRegisteredError as exc:
            return IntentAuthorizationResponse(
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), exc.code),
            )
        except ValueError as exc:
            return IntentAuthorizationResponse(
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        self._pending[mandate_id] = {"request": request_payload, "mandate": mandate}
        return IntentAuthorizationResponse(
            mandate=mandate,
            signingOptions=signing_options,
            approved=False,
        )

    async def complete(self, request: dict[str, Any]) -> IntentAuthorizationResponse:
        signed_mandate = request.get("signedMandate") if isinstance(request.get("signedMandate"), dict) else None
        if signed_mandate is None:
            return IntentAuthorizationResponse(
                approved=False,
                rejectReason="signedMandate missing",
                verificationResult=VerificationResult.fail("signedMandate missing", "MANDATE_INVALID"),
            )
        try:
            mandate_id = mandate_identifier(signed_mandate)
        except ValueError as exc:
            return IntentAuthorizationResponse(
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        pending = self._pending.get(mandate_id)
        if pending is None:
            return IntentAuthorizationResponse(
                approved=False,
                rejectReason=f"No pending intent authorization: {mandate_id}",
                verificationResult=VerificationResult.fail("No pending intent authorization", "AUTHORIZATION_NOT_PENDING"),
            )
        payload = pending["request"]
        mandate = pending["mandate"]
        if not mandate_matches_pending(
            signed_mandate,
            mandate,
            normalize=normalize_intent_mandate,
        ):
            reason = "Signed mandate does not match pending intent authorization"
            return IntentAuthorizationResponse(
                mandate=mandate,
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, "MANDATE_PENDING_MISMATCH"),
            )
        valid, reason = verify_intent_mandate(
            signed_mandate,
            expected_server=str(payload.get("server") or self.server_id),
            expected_user_id=str(payload.get("userId") or ""),
            require_user_signature=self.require_user_signature,
            user_signature_method=self.user_signature_method,
        )
        if not valid:
            return IntentAuthorizationResponse(
                mandate=mandate,
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, error_code(reason, "MANDATE")),
            )
        self._pending.pop(mandate_id, None)
        token = issue_intent_token(
            signed_mandate,
            user_id=str(payload.get("userId") or "user:unknown"),
            verified_mandate=True,
        )
        return IntentAuthorizationResponse(
            mandate=signed_mandate,
            intentToken=token,
            approved=True,
            verificationResult=VerificationResult.ok(),
        )

    async def verify_token(
        self,
        request: TokenVerificationRequest | dict[str, Any],
    ) -> TokenVerificationResponse:
        request_payload = payload(request)
        token = request_payload.get("token") if isinstance(request_payload.get("token"), dict) else {}
        expected = request_payload.get("expected") if isinstance(request_payload.get("expected"), dict) else {}
        valid, reason = verify_intent_token(
            token,
            action=str(expected.get("action") or ""),
            params=expected.get("params") if isinstance(expected.get("params"), dict) else {},
            expected_agent_id=(str(expected.get("agentId")) if expected.get("agentId") is not None else None),
            expected_user_id=(str(expected.get("userId")) if expected.get("userId") is not None else None),
            expected_agent_key_id=(str(expected.get("agentKeyId")) if expected.get("agentKeyId") is not None else None),
        )
        if not valid:
            return TokenVerificationResponse(valid=False, reason=reason, code=error_code(reason, "TOKEN"))
        try:
            consumed, usage_or_reason = self._consume_token_usage(token)
        except IntentTokenUsageStoreError:
            return TokenVerificationResponse(
                valid=False,
                reason="Intent token usage store unavailable",
                code="TOKEN_USAGE_STORE_ERROR",
            )
        if not consumed:
            return TokenVerificationResponse(
                valid=False,
                reason=usage_or_reason,
                code="TOKEN_USAGE_EXCEEDED",
            )
        usage = usage_or_reason if isinstance(usage_or_reason, dict) else {}
        return TokenVerificationResponse(
            valid=True,
            matchedScope={
                "action": expected.get("action"),
                "params": expected.get("params") if isinstance(expected.get("params"), dict) else {},
                **usage,
            },
        )

    def _consume_token_usage(self, token: dict[str, Any]) -> tuple[bool, dict[str, Any] | str]:
        token_id = str(token.get("tokenId") or "").strip()
        if not token_id:
            return False, "Token tokenId missing"
        try:
            intent = normalize_intent_scope(token.get("intent"))
        except ValueError as exc:
            return False, str(exc)
        policy = intent.get("executionPolicy") if isinstance(intent.get("executionPolicy"), dict) else None
        if policy is None:
            return True, {}

        max_executions = int(policy["maxExecutions"])
        expire_at = str(token.get("expireAt") or "").strip()
        try:
            expire_at_epoch = calendar.timegm(time.strptime(expire_at, "%Y-%m-%dT%H:%M:%SZ"))
        except ValueError as exc:
            raise IntentTokenUsageStoreError("Intent token expiration is invalid") from exc
        consumed, executions_used = self.token_usage_store.consume(
            token_id=token_id,
            max_executions=max_executions,
            expire_at_epoch=expire_at_epoch,
        )
        if not consumed:
            return False, "Token execution usage exceeded"
        return True, {
            "usage": {
                "executionsUsed": executions_used,
                "executionsLimit": max_executions,
            }
        }


__all__ = ["IntentAuthorizationService"]
