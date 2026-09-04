"""Public contracts and helpers for A4P user-signature methods."""

from a4p.user_signature.contracts import (
    A4PUserSignatureMethod,
    A4PUserSigner,
    UserSignature,
    UserSignatureContext,
    UserSigningInput,
    attach_user_signature,
    canonical_user_authorization_payload,
    sign_user_signature,
    user_authorization_challenge,
    verify_user_signature,
)

__all__ = [
    "A4PUserSignatureMethod",
    "A4PUserSigner",
    "UserSignature",
    "UserSignatureContext",
    "UserSigningInput",
    "attach_user_signature",
    "canonical_user_authorization_payload",
    "sign_user_signature",
    "user_authorization_challenge",
    "verify_user_signature",
]
