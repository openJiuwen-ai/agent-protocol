"""A4P user authorization boundary."""

from __future__ import annotations

from typing import Any, Protocol

from a4p.intent_mandate import sign_user_intent_mandate
from a4p.operation_mandate import sign_user_mandate as sign_user_operation_mandate
from a4p.types import UserAuthorizationRequest, UserAuthorizationResponse


def sign_user_mandate_for_request(
    mandate: dict[str, Any],
    *,
    request_id: str,
    approval_method: str = "chat.user_answer",
    approved_at: str | None = None,
) -> dict[str, Any]:
    """Sign an approved A4P mandate using the correct mandate-type signer."""
    mandate_type = mandate.get("type")
    if mandate_type == "a4p/v1/intent-mandate":
        return sign_user_intent_mandate(
            mandate,
            request_id=request_id,
            approval_method=approval_method,
            approved_at=approved_at,
        )
    if mandate_type == "a4p/v1/operation-mandate":
        return sign_user_operation_mandate(
            mandate,
            request_id=request_id,
            approval_method=approval_method,
            approved_at=approved_at,
        )
    raise ValueError(f"Unsupported A4P mandate type: {mandate_type!r}")


class A4PUserAuthorizer(Protocol):
    """Device/UI-side user authorization interface.

    Implementations decide whether the user approves the mandate. If approved,
    return a user-signed mandate. Use sign_user_mandate_for_request() unless a
    custom signing backend is needed.
    """

    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        """Ask the user to approve the mandate and return the authorization result."""


class RejectingA4PUserAuthorizer:
    """Default authorizer used when no UI bridge is installed."""

    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        return UserAuthorizationResponse(
            requestId=request.requestId,
            approved=False,
            rejectReason="A4P user authorizer is not configured",
        )


class ApprovingA4PUserAuthorizer:
    """Authorizer for tests and trusted local flows that approve every mandate."""

    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        signed = sign_user_mandate_for_request(
            request.mandate,
            request_id=request.requestId,
        )
        return UserAuthorizationResponse(
            requestId=request.requestId,
            approved=True,
            signedMandate=signed,
        )


__all__ = [
    "A4PUserAuthorizer",
    "ApprovingA4PUserAuthorizer",
    "RejectingA4PUserAuthorizer",
    "sign_user_mandate_for_request",
]
