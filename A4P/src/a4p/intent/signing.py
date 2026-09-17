"""Intent mandate and token Server-signing configuration."""

from __future__ import annotations

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p.security import ed25519_private_key_from_env, ed25519_public_key_to_base64url


INTENT_SERVER_SIGN_ALGORITHM = "EdDSA"
INTENT_MANDATE_SERVER_KEY_ID = "server#intent-mandate-k1"
INTENT_SERVER_PRIVATE_KEY_LABEL = "a4p-intent-server-ed25519-dev-v1"


def intent_server_signing_key() -> Ed25519PrivateKey:
    return ed25519_private_key_from_env(
        env_name="INTENT_SERVER_ED25519_PRIVATE_KEY",
        default_seed_label=INTENT_SERVER_PRIVATE_KEY_LABEL,
        purpose="intent mandate server Ed25519 signing key",
    )


def intent_server_trusted_key() -> dict[str, str]:
    """Return the public trust entry for the current Intent Server signing key."""
    return {
        "alg": INTENT_SERVER_SIGN_ALGORITHM,
        "keyId": INTENT_MANDATE_SERVER_KEY_ID,
        "publicKey": ed25519_public_key_to_base64url(
            intent_server_signing_key().public_key()
        ),
    }


__all__ = [
    "INTENT_MANDATE_SERVER_KEY_ID",
    "INTENT_SERVER_PRIVATE_KEY_LABEL",
    "INTENT_SERVER_SIGN_ALGORITHM",
    "intent_server_signing_key",
    "intent_server_trusted_key",
]
