"""Server-side A4P role implementation."""

from __future__ import annotations

import uuid
from dataclasses import is_dataclass
from typing import Any

from a4p.intent_mandate import (
    create_intent_mandate,
    issue_intent_token,
    verify_intent_mandate,
    verify_intent_token,
)
from a4p.operation_mandate import (
    create_operation_mandate,
    verify_operation_mandate,
)
from a4p.types import (
    IntentAuthorizationRequest,
    IntentAuthorizationResponse,
    OperationAuthorizationRequest,
    OperationAuthorizationResponse,
    TokenVerificationRequest,
    TokenVerificationResponse,
    UserAuthorizationRequest,
    VerificationResult,
    to_payload,
)
from a4p.user_authorizer import A4PUserAuthorizer, RejectingA4PUserAuthorizer


def _payload(value: Any) -> dict[str, Any]:
    converted = to_payload(value) if is_dataclass(value) else value
    return converted if isinstance(converted, dict) else {}


def _error_code(reason: str, prefix: str) -> str:
    lower = reason.lower()
    if "expired" in lower:
        return f"{prefix}_EXPIRED"
    if "signature" in lower:
        return f"{prefix}_SIGNATURE_INVALID"
    if "mismatch" in lower or "scope" in lower or "actions" in lower or "param" in lower:
        return f"{prefix}_SCOPE_MISMATCH"
    return f"{prefix}_INVALID"


class A4PServer:
    """A4P server role: create mandates, wait for user authorization, verify and issue tokens."""

    def __init__(
        self,
        *,
        user_authorizer: A4PUserAuthorizer | None = None,
        server_id: str = "local://a4p",
    ) -> None:
        self.user_authorizer = user_authorizer or RejectingA4PUserAuthorizer()
        self.server_id = server_id

    async def authorize_intent(
        self,
        request: IntentAuthorizationRequest | dict[str, Any],
    ) -> IntentAuthorizationResponse:
        payload = _payload(request)
        request_id = str(payload.get("requestId") or f"a4p_intent_{uuid.uuid4().hex[:12]}")
        intent = payload.get("intent") if isinstance(payload.get("intent"), dict) else {}
        try:
            mandate = create_intent_mandate(
                server=str(payload.get("server") or self.server_id),
                agent_id=str(payload.get("agentId") or ""),
                actions=intent.get("actions"),
                validity_seconds=int(payload.get("validitySeconds") or 3600),
            )
        except ValueError as exc:
            return IntentAuthorizationResponse(
                requestId=request_id,
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        auth_response = await self.user_authorizer.authorize(
            UserAuthorizationRequest(
                requestId=request_id,
                mandate=mandate,
                uiContext={"kind": "intent", **dict(payload.get("metadata") or {})},
            )
        )
        if not auth_response.approved or not isinstance(auth_response.signedMandate, dict):
            return IntentAuthorizationResponse(
                requestId=request_id,
                mandate=mandate,
                approved=False,
                rejectReason=auth_response.rejectReason or "Authorization rejected",
                verificationResult=VerificationResult.fail(
                    auth_response.rejectReason or "Authorization rejected",
                    "AUTHORIZATION_REJECTED",
                ),
            )
        valid, reason = verify_intent_mandate(auth_response.signedMandate, expected_server=str(payload.get("server") or self.server_id))
        if not valid:
            return IntentAuthorizationResponse(
                requestId=request_id,
                mandate=mandate,
                approved=False,
                rejectReason=reason,
                verificationResult=VerificationResult.fail(reason, _error_code(reason, "MANDATE")),
            )
        token = issue_intent_token(
            auth_response.signedMandate,
            user_id=str(payload.get("userId") or "user:unknown"),
        )
        return IntentAuthorizationResponse(
            requestId=request_id,
            mandate=auth_response.signedMandate,
            intentToken=token,
            approved=True,
            verificationResult=VerificationResult.ok(),
        )

    async def verify_intent_token(
        self,
        request: TokenVerificationRequest | dict[str, Any],
    ) -> TokenVerificationResponse:
        payload = _payload(request)
        token = payload.get("token") if isinstance(payload.get("token"), dict) else {}
        expected = payload.get("expected") if isinstance(payload.get("expected"), dict) else {}
        valid, reason = verify_intent_token(
            token,
            action=str(expected.get("action") or ""),
            params=expected.get("params") if isinstance(expected.get("params"), dict) else {},
            expected_agent_id=(str(expected.get("agentId")) if expected.get("agentId") is not None else None),
            expected_user_id=(str(expected.get("userId")) if expected.get("userId") is not None else None),
        )
        if valid:
            return TokenVerificationResponse(
                valid=True,
                matchedScope={
                    "action": expected.get("action"),
                    "params": expected.get("params") if isinstance(expected.get("params"), dict) else {},
                },
            )
        return TokenVerificationResponse(valid=False, reason=reason, code=_error_code(reason, "TOKEN"))

    async def authorize_operation(
        self,
        request: OperationAuthorizationRequest | dict[str, Any],
    ) -> OperationAuthorizationResponse:
        payload = _payload(request)
        request_id = str(payload.get("requestId") or f"a4p_operation_{uuid.uuid4().hex[:12]}")
        operation = payload.get("operation") if isinstance(payload.get("operation"), dict) else {}
        try:
            mandate = create_operation_mandate(
                operation=operation,
                server_url=str(payload.get("server") or self.server_id),
                agent_id=str(payload.get("agentId") or ""),
                validity_seconds=int(payload.get("validitySeconds") or 300),
            )
        except ValueError as exc:
            return OperationAuthorizationResponse(
                requestId=request_id,
                approved=False,
                rejectReason=str(exc),
                verificationResult=VerificationResult.fail(str(exc), "MANDATE_INVALID"),
            )
        auth_response = await self.user_authorizer.authorize(
            UserAuthorizationRequest(
                requestId=request_id,
                mandate=mandate,
                uiContext={"kind": "operation", **dict(payload.get("metadata") or {})},
            )
        )
        if not auth_response.approved or not isinstance(auth_response.signedMandate, dict):
            return OperationAuthorizationResponse(
                requestId=request_id,
                mandate=mandate,
                approved=False,
                rejectReason=auth_response.rejectReason or "Authorization rejected",
                verificationResult=VerificationResult.fail(
                    auth_response.rejectReason or "Authorization rejected",
                    "AUTHORIZATION_REJECTED",
                ),
            )
        valid, reason = verify_operation_mandate(auth_response.signedMandate, expected=operation)
        verification = VerificationResult.ok() if valid else VerificationResult.fail(reason, _error_code(reason, "MANDATE"))
        return OperationAuthorizationResponse(
            requestId=request_id,
            mandate=mandate,
            signedMandate=auth_response.signedMandate if valid else None,
            verificationResult=verification,
            approved=valid,
            rejectReason=None if valid else reason,
        )


__all__ = ["A4PServer"]
