"""Registered Ed25519 signature method and User Authorizer signer."""

from __future__ import annotations

import secrets
from collections.abc import Mapping
from copy import deepcopy
from dataclasses import asdict
from typing import Any

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p.credential_store import A4PCredentialStore, UserCredentialRecord, utc_now_iso
from a4p.errors import CredentialKeyConflictError, UserCredentialNotRegisteredError
from a4p.security import (
    ed25519_public_key_from_base64url,
    ed25519_public_key_to_base64url,
    ed25519_sign_text,
    ed25519_verify_text,
)
from a4p.user_signature.contracts import (
    UserSignature,
    UserSignatureContext,
    UserSigningInput,
    canonical_user_authorization_payload,
)


ED25519_SIGNATURE_METHOD = "ed25519"


def ed25519_public_jwk(private_key: Ed25519PrivateKey) -> dict[str, str]:
    return {
        "kty": "OKP",
        "crv": "Ed25519",
        "x": ed25519_public_key_to_base64url(private_key.public_key()),
        "alg": "EdDSA",
    }


def _normalize_public_jwk(value: Any) -> dict[str, str]:
    if not isinstance(value, Mapping):
        raise ValueError("publicKey must be an OKP JWK object")
    if "d" in value:
        raise ValueError("publicKey must not contain private key material")
    if value.get("kty") != "OKP":
        raise ValueError("publicKey.kty must be 'OKP'")
    if value.get("crv") != "Ed25519":
        raise ValueError("publicKey.crv must be 'Ed25519'")
    if "alg" in value and value.get("alg") != "EdDSA":
        raise ValueError("publicKey.alg must be 'EdDSA'")
    encoded_key = str(value.get("x") or "").strip()
    ed25519_public_key_from_base64url(encoded_key)
    return {
        "kty": "OKP",
        "crv": "Ed25519",
        "x": encoded_key,
        "alg": "EdDSA",
    }


class RegisteredEd25519Method:
    """Register and verify user-owned Ed25519 public keys."""

    signature_method = ED25519_SIGNATURE_METHOD

    def __init__(self, credential_store: A4PCredentialStore) -> None:
        self.credential_store = credential_store

    def method_policy(self) -> dict[str, Any]:
        return {}

    def register(self, request: Mapping[str, Any]) -> dict[str, Any]:
        user_id = str(request.get("userId") or "").strip()
        if not user_id:
            raise ValueError("userId missing")
        public_key = _normalize_public_jwk(request.get("publicKey"))
        metadata_raw = request.get("metadata")
        if metadata_raw is not None and not isinstance(metadata_raw, Mapping):
            raise ValueError("metadata must be an object")
        metadata = deepcopy(dict(metadata_raw or {}))

        matching: UserCredentialRecord | None = None
        for record in self.credential_store.list_all():
            if (
                record.signatureMethod == self.signature_method
                and record.publicKey == public_key
            ):
                matching = record
                break
        if matching is not None:
            if matching.userId != user_id:
                raise CredentialKeyConflictError()
            return {
                "registered": True,
                "created": False,
                "credential": asdict(matching),
            }

        record = UserCredentialRecord(
            userId=user_id,
            credentialId=f"cred_{secrets.token_urlsafe(32)}",
            signatureMethod=self.signature_method,
            publicKey=public_key,
            metadata=metadata,
            createdAt=utc_now_iso(),
        )
        self.credential_store.save(record)
        return {
            "registered": True,
            "created": True,
            "credential": asdict(record),
        }

    def signing_options(
        self,
        *,
        user_id: str,
        mandate: Mapping[str, Any],
    ) -> dict[str, Any]:
        del mandate
        credential_ids = [
            record.credentialId
            for record in self.credential_store.list_for_user(user_id)
            if record.signatureMethod == self.signature_method
        ]
        if not credential_ids:
            raise UserCredentialNotRegisteredError(
                user_id=user_id,
                signature_method=self.signature_method,
            )
        return {
            "signatureMethod": self.signature_method,
            "methodOptions": {"allowedCredentialIds": credential_ids},
        }

    def verify(
        self,
        context: UserSignatureContext,
        signature: Mapping[str, Any],
    ) -> tuple[bool, str]:
        credential_id = str(signature.get("credentialId") or "").strip()
        if not credential_id:
            return False, "User credentialId missing"
        record = self.credential_store.get(credential_id)
        if record is None:
            return False, f"User credential not registered: {credential_id}"
        if record.signatureMethod != self.signature_method:
            return False, "User credential signature method mismatch"
        if context.expected_user_id is not None and record.userId != context.expected_user_id:
            return False, (
                "User credential user mismatch: "
                f"expected '{context.expected_user_id}', got '{record.userId}'"
            )
        proof = signature.get("proof")
        if not isinstance(proof, Mapping):
            return False, "User signature proof missing"
        if proof.get("alg") != "EdDSA":
            return False, "User signature proof alg must be 'EdDSA'"
        encoded_signature = str(proof.get("signature") or "").strip()
        if not encoded_signature:
            return False, "User signature missing"
        try:
            public_key = ed25519_public_key_from_base64url(
                str(record.publicKey.get("x") or "")
            )
        except ValueError as exc:
            return False, f"Registered Ed25519 public key invalid: {exc}"
        valid = ed25519_verify_text(
            canonical_user_authorization_payload(context),
            encoded_signature,
            public_key,
        )
        return (True, "") if valid else (False, "User signature invalid")


class Ed25519UserSigner:
    """Sign with a caller-managed registered Ed25519 private key."""

    signature_method = ED25519_SIGNATURE_METHOD

    def __init__(
        self,
        *,
        credential_id: str,
        private_key: Ed25519PrivateKey,
    ) -> None:
        normalized_id = credential_id.strip()
        if not normalized_id:
            raise ValueError("credential_id missing")
        self.credential_id = normalized_id
        self.private_key = private_key

    def sign(
        self,
        context: UserSignatureContext,
        *,
        signing_input: UserSigningInput | None = None,
    ) -> UserSignature:
        del signing_input
        return {
            "signatureMethod": self.signature_method,
            "credentialId": self.credential_id,
            "proof": {
                "alg": "EdDSA",
                "signature": ed25519_sign_text(
                    canonical_user_authorization_payload(context),
                    self.private_key,
                ),
            },
        }


__all__ = [
    "ED25519_SIGNATURE_METHOD",
    "Ed25519UserSigner",
    "RegisteredEd25519Method",
    "ed25519_public_jwk",
]
