"""Carrier-neutral user-signature contracts and shared helpers."""

from __future__ import annotations

from collections.abc import Mapping
from copy import deepcopy
from dataclasses import dataclass
from typing import Any, Protocol

from a4p.mandate_security import (
    canonical_user_authorization_payload as canonical_mandate_user_authorization_payload,
    derive_user_authorization_challenge,
)


UserSignature = dict[str, Any]
UserSigningInput = dict[str, Any]


@dataclass(frozen=True)
class UserSignatureContext:
    """The complete Server-signed mandate and expected user binding."""

    mandate_type: str
    server_signed_mandate: dict[str, Any]
    signature_method: str
    expected_user_id: str | None = None


def canonical_user_authorization_payload(context: UserSignatureContext) -> str:
    """Return the common proof input used by every signature method."""
    return canonical_mandate_user_authorization_payload(context.server_signed_mandate)


def user_authorization_challenge(context: UserSignatureContext) -> bytes:
    """Return the WebAuthn challenge for the common proof input."""
    return derive_user_authorization_challenge(context.server_signed_mandate)


class A4PUserSigner(Protocol):
    """User Authorizer-side signer that creates one proof envelope."""

    signature_method: str

    def sign(
        self,
        context: UserSignatureContext,
        *,
        signing_input: UserSigningInput | None = None,
    ) -> UserSignature:
        """Create a user signature envelope for the approved mandate."""


class A4PUserSignatureMethod(Protocol):
    """One Server-side user-signature method selected for an A4PServer."""

    signature_method: str

    def method_policy(self) -> dict[str, Any]:
        """Return the Server-signed policy for this method."""

    def signing_options(
        self,
        *,
        user_id: str,
        mandate: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Return method-specific options for the User Authorizer."""

    def verify(
        self,
        context: UserSignatureContext,
        signature: Mapping[str, Any],
    ) -> tuple[bool, str]:
        """Verify one user proof and its credential binding."""


def attach_user_signature(
    mandate: Mapping[str, Any],
    signature: Mapping[str, Any],
) -> dict[str, Any]:
    signed = deepcopy(dict(mandate))
    signatures = signed.get("signatures")
    signed["signatures"] = dict(signatures) if isinstance(signatures, dict) else {}
    signed["signatures"]["user"] = deepcopy(dict(signature))
    return signed


def sign_user_signature(
    context: UserSignatureContext,
    *,
    user_signer: A4PUserSigner,
    signing_input: UserSigningInput | None = None,
) -> UserSignature:
    if user_signer.signature_method != context.signature_method:
        raise ValueError(
            "User signer method mismatch: "
            f"mandate requires '{context.signature_method}', got "
            f"'{user_signer.signature_method}'"
        )
    signature = user_signer.sign(context, signing_input=signing_input)
    if signature.get("signatureMethod") != context.signature_method:
        raise ValueError("User signer returned a mismatched signatureMethod")
    return signature


def verify_user_signature(
    context: UserSignatureContext,
    signature: Mapping[str, Any],
    *,
    method: A4PUserSignatureMethod | None,
    require_user_signature: bool,
) -> tuple[bool, str]:
    if not require_user_signature:
        return (True, "") if not signature else (False, "User signature must be empty")
    if method is None:
        return False, "User signature method missing"
    signature_method = str(signature.get("signatureMethod") or "").strip()
    if not signature_method:
        return False, "User signature missing"
    if signature_method != context.signature_method:
        return False, (
            "User signature method mismatch: "
            f"mandate requires '{context.signature_method}', got '{signature_method}'"
        )
    if method.signature_method != context.signature_method:
        return False, (
            "Configured user signature method mismatch: "
            f"mandate requires '{context.signature_method}', configured method is "
            f"'{method.signature_method}'"
        )
    return method.verify(context, signature)


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
