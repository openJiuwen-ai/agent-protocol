"""Operation mandate Server-signing configuration."""

from __future__ import annotations

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p.security import ed25519_private_key_from_env, ed25519_public_key_to_base64url


OPERATION_SERVER_SIGN_ALGORITHM = "EdDSA"
OPERATION_MANDATE_SERVER_KEY_ID = "server#operation-mandate-k1"
OPERATION_SERVER_PRIVATE_KEY_LABEL = "a4p-operation-server-ed25519-dev-v1"


def operation_server_signing_key() -> Ed25519PrivateKey:
    return ed25519_private_key_from_env(
        env_name="OPERATION_SERVER_ED25519_PRIVATE_KEY",
        default_seed_label=OPERATION_SERVER_PRIVATE_KEY_LABEL,
        purpose="operation mandate server Ed25519 signing key",
    )


def operation_server_trusted_key() -> dict[str, str]:
    """Return the public trust entry for the current Operation Server signing key."""
    return {
        "alg": OPERATION_SERVER_SIGN_ALGORITHM,
        "keyId": OPERATION_MANDATE_SERVER_KEY_ID,
        "publicKey": ed25519_public_key_to_base64url(
            operation_server_signing_key().public_key()
        ),
    }


__all__ = [
    "OPERATION_MANDATE_SERVER_KEY_ID",
    "OPERATION_SERVER_PRIVATE_KEY_LABEL",
    "OPERATION_SERVER_SIGN_ALGORITHM",
    "operation_server_signing_key",
    "operation_server_trusted_key",
]
