"""A4P user authorization boundary."""

from __future__ import annotations

from copy import deepcopy
from typing import Protocol, cast

from a4p.intent.mandate import intent_user_signature_context, normalize_intent_mandate
from a4p.mandate_security import (
    MandateSecurityError,
    StaticA4PServerTrustStore,
    user_authorization_challenge_base64url,
    verify_mandate_valid_time,
    verify_trusted_server_mandate,
)
from a4p.operation.mandate import normalize_operation_mandate, operation_user_signature_context
from a4p.types import IntentMandate, OperationMandate, UserAuthorizationRequest, UserAuthorizationResponse
from a4p.user_signature import (
    A4PUserSigner,
    UserSigningInput,
    attach_user_signature,
    sign_user_signature,
)


def verify_local_user_authorization_request(
    request: UserAuthorizationRequest,
    *,
    trust_store: StaticA4PServerTrustStore,
    expected_signature_method: str | None = None,
) -> dict[str, object]:
    """Verify a request and return locally hardened signing options."""
    core = verify_trusted_server_mandate(request.mandate, trust_store)
    verify_mandate_valid_time(core)
    user_authorization = core.get("userAuthorization")
    if not isinstance(user_authorization, dict):
        raise MandateSecurityError("CHALLENGE_BINDING_INVALID", "userAuthorization missing")
    required = user_authorization.get("required")
    if not isinstance(required, bool):
        raise MandateSecurityError(
            "CHALLENGE_BINDING_INVALID",
            "userAuthorization.required missing",
        )
    if not required:
        return {}
    signature_method = str(user_authorization.get("signatureMethod") or "").strip()
    if not signature_method:
        raise MandateSecurityError(
            "CHALLENGE_BINDING_INVALID",
            "userAuthorization.signatureMethod missing",
        )
    if (
        expected_signature_method is not None
        and signature_method != expected_signature_method
    ):
        raise MandateSecurityError(
            "SIGNING_OPTIONS_MISMATCH",
            "User signer method mismatch: "
            f"mandate requires '{signature_method}', local signer is "
            f"'{expected_signature_method}'",
        )
    signing_options = deepcopy(request.signingOptions)
    if signing_options.get("signatureMethod") != signature_method:
        raise MandateSecurityError(
            "SIGNING_OPTIONS_MISMATCH",
            "signingOptions.signatureMethod does not match the Server-signed mandate",
        )
    method_options = signing_options.get("methodOptions")
    if not isinstance(method_options, dict):
        raise MandateSecurityError(
            "SIGNING_OPTIONS_MISMATCH",
            "signingOptions.methodOptions missing",
        )
    if signature_method == "webauthn":
        method_options["challenge"] = user_authorization_challenge_base64url(
            request.mandate
        )
        method_options["userVerification"] = "required"
    return signing_options


def sign_user_mandate_with_signer(
    mandate: IntentMandate | OperationMandate,
    *,
    user_signer: A4PUserSigner,
    signing_input: UserSigningInput | None = None,
) -> IntentMandate | OperationMandate:
    mandate_type = mandate["type"]
    if mandate_type == "a4p/v1/intent-mandate":
        normalized = normalize_intent_mandate(mandate)
        context = intent_user_signature_context(normalized)
    elif mandate_type == "a4p/v1/operation-mandate":
        normalized = normalize_operation_mandate(mandate)
        context = operation_user_signature_context(normalized)
    else:
        raise ValueError(f"Unsupported A4P mandate type: {mandate_type!r}")
    signature = sign_user_signature(
        context,
        user_signer=user_signer,
        signing_input=signing_input,
    )
    return cast(
        IntentMandate | OperationMandate,
        attach_user_signature(normalized, signature),
    )


def approve_user_mandate(
    mandate: IntentMandate | OperationMandate,
) -> IntentMandate | OperationMandate:
    """Return an unsigned mandate for explicit no-signature test mode."""
    approved = deepcopy(mandate)
    approved.setdefault("signatures", {})
    approved["signatures"]["user"] = {}
    return approved


class A4PUserAuthorizer(Protocol):
    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        """Ask the user to approve the mandate and return the result."""


class RejectingA4PUserAuthorizer:
    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        del request
        return UserAuthorizationResponse(
            approved=False,
            rejectReason="A4P user authorizer is not configured",
        )


class ApprovingA4PUserAuthorizer:
    """Test authorizer that approves with an explicitly configured signer."""

    def __init__(self, user_signer: A4PUserSigner) -> None:
        self.user_signer = user_signer

    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        signed = sign_user_mandate_with_signer(
            request.mandate,
            user_signer=self.user_signer,
        )
        return UserAuthorizationResponse(
            approved=True,
            signedMandate=signed,
        )


__all__ = [
    "A4PUserAuthorizer",
    "ApprovingA4PUserAuthorizer",
    "RejectingA4PUserAuthorizer",
    "approve_user_mandate",
    "sign_user_mandate_with_signer",
    "verify_local_user_authorization_request",
]
