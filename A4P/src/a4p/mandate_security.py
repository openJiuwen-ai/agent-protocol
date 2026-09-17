"""Shared mandate challenge binding and local A4P Server trust verification."""

from __future__ import annotations

import base64
import calendar
import hashlib
import json
import time
from collections.abc import Mapping
from copy import deepcopy
from pathlib import Path
from typing import Any

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

from a4p.security import ed25519_public_key_from_base64url, ed25519_verify_text


USER_AUTHORIZATION_SCOPE = "a4p/v1/user-authorization"
SERVER_SIGNATURE_ALGORITHM = "EdDSA"


class MandateSecurityError(ValueError):
    """A fail-closed local mandate verification error with a stable code."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


def canonical_json(payload: Mapping[str, Any]) -> str:
    """Return the canonical JSON representation used by A4P signatures and commitments."""
    return json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )


class StaticA4PServerTrustStore:
    """Static local trust anchors indexed by serverId and server signature keyId."""

    def __init__(self, config: Mapping[str, Any]) -> None:
        trusted: dict[tuple[str, str], tuple[str, Ed25519PublicKey]] = {}
        for server_id, keys_value in config.items():
            if not isinstance(server_id, str) or not server_id.strip():
                raise ValueError("Trusted A4P serverId must be a non-empty string")
            if not isinstance(keys_value, Mapping) or not keys_value:
                raise ValueError(f"Trusted A4P server '{server_id}' must contain keys")
            for key_id, key_value in keys_value.items():
                if not isinstance(key_id, str) or not key_id.strip():
                    raise ValueError("Trusted A4P keyId must be a non-empty string")
                if not isinstance(key_value, Mapping):
                    raise ValueError(f"Trusted A4P key '{key_id}' must be an object")
                alg = str(key_value.get("alg") or "").strip()
                if alg != SERVER_SIGNATURE_ALGORITHM:
                    raise ValueError(
                        f"Trusted A4P key '{key_id}' must use '{SERVER_SIGNATURE_ALGORITHM}'"
                    )
                public_key_value = str(key_value.get("publicKey") or "").strip()
                public_key = ed25519_public_key_from_base64url(public_key_value)
                trusted[(server_id.strip(), key_id.strip())] = (alg, public_key)
        if not trusted:
            raise ValueError("Trusted A4P server key configuration must not be empty")
        self._trusted = trusted

    @classmethod
    def from_json_file(cls, path: str | Path) -> "StaticA4PServerTrustStore":
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("Trusted A4P server key file must contain a JSON object")
        return cls(payload)

    def resolve(self, *, server_id: str, key_id: str, alg: str) -> Ed25519PublicKey:
        entry = self._trusted.get((server_id, key_id))
        if entry is None:
            raise MandateSecurityError(
                "SERVER_KEY_UNTRUSTED",
                f"A4P Server key is not trusted: server='{server_id}', keyId='{key_id}'",
            )
        trusted_alg, public_key = entry
        if alg != trusted_alg:
            raise MandateSecurityError(
                "SERVER_KEY_UNTRUSTED",
                f"A4P Server signature algorithm is not trusted: '{alg}'",
            )
        return public_key


def mandate_core_payload(mandate: Mapping[str, Any]) -> dict[str, Any]:
    """Normalize either A4P mandate type and return its complete signed core."""
    mandate_type = mandate.get("type")
    if mandate_type == "a4p/v1/intent-mandate":
        from a4p.intent.mandate import intent_mandate_core_payload

        return intent_mandate_core_payload(mandate)
    if mandate_type == "a4p/v1/operation-mandate":
        from a4p.operation.mandate import operation_mandate_core_payload

        return operation_mandate_core_payload(mandate)
    raise MandateSecurityError(
        "SERVER_SIGNATURE_INVALID",
        f"Unsupported A4P mandate type: {mandate_type!r}",
    )


def mandate_identifier(mandate: Mapping[str, Any]) -> str:
    """Return the authorization identifier carried by either mandate type."""
    mandate_type = mandate.get("type")
    field_name = "mandateId" if mandate_type == "a4p/v1/intent-mandate" else "operationId"
    if mandate_type not in {"a4p/v1/intent-mandate", "a4p/v1/operation-mandate"}:
        raise ValueError(f"Unsupported A4P mandate type: {mandate_type!r}")
    identifier = str(mandate.get(field_name) or "").strip()
    if not identifier:
        raise ValueError(f"{field_name} missing")
    return identifier


def server_signed_mandate_without_user_signature(
    mandate: Mapping[str, Any],
) -> dict[str, Any]:
    """Return the canonical WebAuthn input: signed mandate with no user signature."""
    core = mandate_core_payload(mandate)
    signatures = mandate.get("signatures")
    server_signature = signatures.get("server") if isinstance(signatures, Mapping) else None
    if not isinstance(server_signature, Mapping):
        raise ValueError("Server signature missing")
    return {
        **core,
        "signatures": {"server": deepcopy(dict(server_signature))},
    }


def canonical_user_authorization_payload(mandate: Mapping[str, Any]) -> str:
    """Return the common proof input for all user-signature methods."""
    return canonical_json(
        {
            "scope": USER_AUTHORIZATION_SCOPE,
            "mandate": server_signed_mandate_without_user_signature(mandate),
        }
    )


def derive_user_authorization_challenge(mandate: Mapping[str, Any]) -> bytes:
    """Return the SHA-256 WebAuthn challenge for the common proof input."""
    return hashlib.sha256(canonical_user_authorization_payload(mandate).encode("utf-8")).digest()


def user_authorization_challenge_base64url(mandate: Mapping[str, Any]) -> str:
    return (
        base64.urlsafe_b64encode(derive_user_authorization_challenge(mandate))
        .decode("ascii")
        .rstrip("=")
    )


def verify_trusted_server_mandate(
    mandate: Mapping[str, Any],
    trust_store: StaticA4PServerTrustStore,
) -> dict[str, Any]:
    """Verify a mandate using only locally configured A4P Server trust anchors."""
    core = mandate_core_payload(mandate)
    server_id = str(core.get("server") or "").strip()
    signatures = mandate.get("signatures")
    server_signature = signatures.get("server") if isinstance(signatures, Mapping) else None
    if not isinstance(server_signature, Mapping):
        raise MandateSecurityError("SERVER_SIGNATURE_INVALID", "Server signature missing")
    alg = str(server_signature.get("alg") or "").strip()
    key_id = str(server_signature.get("keyId") or "").strip()
    signature = str(server_signature.get("signature") or "").strip()
    if not server_id or not alg or not key_id or not signature:
        raise MandateSecurityError("SERVER_SIGNATURE_INVALID", "Server signature metadata missing")
    public_key = trust_store.resolve(server_id=server_id, key_id=key_id, alg=alg)
    payload = canonical_json({"scope": "server", "mandate": core})
    if not ed25519_verify_text(payload, signature, public_key):
        raise MandateSecurityError("SERVER_SIGNATURE_INVALID", "Server signature invalid")
    return core


def verify_mandate_valid_time(mandate_core: Mapping[str, Any]) -> None:
    """Perform the local fail-closed validity check for either mandate type."""
    valid_time = mandate_core.get("validTime")
    if not isinstance(valid_time, Mapping):
        raise MandateSecurityError("CHALLENGE_BINDING_INVALID", "Mandate validTime missing")
    mandate_type = mandate_core.get("type")
    values = [valid_time.get("start"), valid_time.get("end")] if mandate_type == "a4p/v1/intent-mandate" else [valid_time.get("until")]
    try:
        timestamps = [
            calendar.timegm(time.strptime(str(value), "%Y-%m-%dT%H:%M:%SZ"))
            for value in values
            if value
        ]
    except ValueError as exc:
        raise MandateSecurityError("CHALLENGE_BINDING_INVALID", "Mandate validTime format invalid") from exc
    if len(timestamps) != len(values):
        raise MandateSecurityError("CHALLENGE_BINDING_INVALID", "Mandate validTime missing")
    now = time.time()
    if mandate_type == "a4p/v1/intent-mandate" and now < timestamps[0]:
        raise MandateSecurityError("CHALLENGE_BINDING_INVALID", "Mandate not yet valid")
    if now > timestamps[-1]:
        raise MandateSecurityError("CHALLENGE_BINDING_INVALID", "Mandate has expired")


__all__ = [
    "MandateSecurityError",
    "StaticA4PServerTrustStore",
    "USER_AUTHORIZATION_SCOPE",
    "canonical_json",
    "canonical_user_authorization_payload",
    "derive_user_authorization_challenge",
    "mandate_core_payload",
    "mandate_identifier",
    "server_signed_mandate_without_user_signature",
    "verify_mandate_valid_time",
    "verify_trusted_server_mandate",
    "user_authorization_challenge_base64url",
]
