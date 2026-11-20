"""A4P Server orchestration for one-time operation authorization."""

from __future__ import annotations

import calendar
import time
from copy import deepcopy
from typing import Any

from a4p.authorization_common import error_code, mandate_matches_pending, payload
from a4p.errors import UserCredentialNotRegisteredError
from a4p.mandate_security import mandate_identifier
from a4p.operation.mandate import (
    OperationDisplayTextRenderer,
    create_operation_mandate,
    normalize_operation_mandate,
    verify_operation_mandate_for_completion,
)
from a4p.types import (
    OperationAuthorizationChallenge,
    OperationAuthorizationCompletionRequest,
    OperationAuthorizationRequest,
    OperationAuthorizationResult,
    VerificationResult,
)
from a4p.user_signature import A4PUserSignatureMethod


def _normalize_operation(operation: Any) -> dict[str, Any]:
    if not isinstance(operation, dict):
        raise ValueError("Operation must be an object")
    action = str(operation.get("action") or "").strip()
    if not action:
        raise ValueError("Operation action missing")
    params_raw = operation.get("params")
    if params_raw is None:
        params: dict[str, Any] = {}
    elif isinstance(params_raw, dict):
        params = deepcopy(params_raw)
    else:
        raise ValueError("Operation params must be an object")
    return {"action": action, "params": params}


class OperationAuthorizationService:
    """Prepare, validate, and consume one-time operation mandates."""

    def __init__(
        self,
        *,
        server_id: str,
        display_text_renderer: OperationDisplayTextRenderer | None,
        require_user_signature: bool,
        user_signature_method: A4PUserSignatureMethod | None,
    ) -> None:
        self.server_id = server_id
        self.display_text_renderer = display_text_renderer
        self.require_user_signature = require_user_signature
        self.user_signature_method = user_signature_method
        self._pending: dict[str, dict[str, Any]] = {}

    async def prepare(
        self,
        request: OperationAuthorizationRequest | dict[str, Any],
    ) -> OperationAuthorizationChallenge:
        request_payload = payload(request)
        self._prune_expired()
        try:
            agent_id = str(request_payload.get("agentId") or "").strip()
            if not agent_id:
                raise ValueError("agentId missing")
            user_id = str(request_payload.get("userId") or "").strip()
            if not user_id:
                raise ValueError("userId missing")
            validity_raw = request_payload.get("validitySeconds")
            if validity_raw is None:
                validity_seconds = 300
            elif isinstance(validity_raw, bool) or not isinstance(validity_raw, int) or validity_raw <= 0:
                raise ValueError("validitySeconds must be a positive integer")
            else:
                validity_seconds = validity_raw
            operation = _normalize_operation(request_payload.get("operation"))
            mandate = create_operation_mandate(
                operation=operation,
                server_url=str(request_payload.get("server") or self.server_id),
                agent_id=agent_id,
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
            operation_id = mandate_identifier(mandate)
            signing_options = (
                self.user_signature_method.signing_options(
                    user_id=user_id,
                    mandate=mandate,
                )
                if self.require_user_signature and self.user_signature_method is not None
                else {}
            )
        except UserCredentialNotRegisteredError as exc:
            return OperationAuthorizationChallenge(
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), exc.code),
            )
        except ValueError as exc:
            return OperationAuthorizationChallenge(
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        expire_at_epoch = calendar.timegm(time.strptime(mandate["validTime"]["until"], "%Y-%m-%dT%H:%M:%SZ"))
        self._pending[operation_id] = {
            "request": request_payload,
            "operation": operation,
            "mandate": mandate,
            "expireAtEpoch": expire_at_epoch,
        }
        return OperationAuthorizationChallenge(mandate=mandate, signingOptions=signing_options)

    async def complete(
        self,
        request: OperationAuthorizationCompletionRequest | dict[str, Any],
    ) -> OperationAuthorizationResult:
        self._prune_expired()
        request_payload = payload(request)
        signed_mandate = request_payload.get("signedMandate") if isinstance(request_payload.get("signedMandate"), dict) else None
        if signed_mandate is None:
            return OperationAuthorizationResult(
                approved=False,
                rejectReason="signedMandate missing",
                verificationResult=VerificationResult.fail("signedMandate missing", "MANDATE_INVALID"),
            )
        try:
            operation_id = mandate_identifier(signed_mandate)
        except ValueError as exc:
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        pending = self._pending.get(operation_id)
        if pending is None:
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=f"No pending operation authorization: {operation_id}",
                verificationResult=VerificationResult.fail("No pending operation authorization", "AUTHORIZATION_NOT_PENDING"),
            )
        mandate = pending["mandate"]
        prepared_request = pending["request"]
        prepared_operation = pending["operation"]
        try:
            current_operation = _normalize_operation(request_payload.get("operation"))
        except ValueError as exc:
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "OPERATION_INVALID"),
            )
        if current_operation != prepared_operation:
            reason = "Current operation does not match pending operation authorization"
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, "OPERATION_PENDING_MISMATCH"),
            )
        if not mandate_matches_pending(
            signed_mandate,
            mandate,
            normalize=normalize_operation_mandate,
        ):
            reason = "Signed mandate does not match pending operation authorization"
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, "MANDATE_PENDING_MISMATCH"),
            )
        try:
            signed_operation = _normalize_operation(signed_mandate.get("operation"))
        except ValueError as exc:
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        if signed_operation != current_operation:
            reason = "Signed mandate operation does not match current operation"
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, "OPERATION_MANDATE_MISMATCH"),
            )
        valid, reason = verify_operation_mandate_for_completion(
            signed_mandate,
            expected=current_operation,
            expected_user_id=str(prepared_request.get("userId") or ""),
            require_user_signature=self.require_user_signature,
            user_signature_method=self.user_signature_method,
        )
        if not valid:
            return OperationAuthorizationResult(
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, error_code(reason, "MANDATE")),
            )
        self._pending.pop(operation_id, None)
        return OperationAuthorizationResult(
            operationId=operation_id,
            verificationResult=VerificationResult.ok(),
            approved=True,
        )

    def _prune_expired(self) -> None:
        now = int(time.time())
        expired = [
            operation_id
            for operation_id, pending in self._pending.items()
            if int(pending.get("expireAtEpoch") or 0) <= now
        ]
        for operation_id in expired:
            self._pending.pop(operation_id, None)


__all__ = ["OperationAuthorizationService"]
