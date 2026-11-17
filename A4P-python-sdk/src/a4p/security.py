"""Security helpers for A4P signing configuration."""

from __future__ import annotations

import base64
import binascii
import hashlib
import logging
import os
import re
import secrets

from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey


logger = logging.getLogger(__name__)

_PRODUCTION_ENV_VALUES = {"prod", "production"}
_WARNED_DEFAULT_KEYS: set[str] = set()

# Environments that explicitly opt into the built-in development signing key.
# Anything else -- including "no environment variable set at all" -- is treated
# as a real deployment, i.e. the fallback below is denied.
_DEV_KEY_ENV_VALUES = {"dev", "development", "local", "test", "testing", "ci"}
_DEV_KEY_OPT_IN_VAR = "A4P_ALLOW_DEV_KEYS"
_DEV_KEY_OPT_IN_VALUES = {"1", "true", "yes", "on"}


def random_challenge() -> str:
    """Return a cryptographically secure, URL-safe authorization challenge."""
    return secrets.token_urlsafe(32)


def is_production_environment() -> bool:
    for env_name in ("A4P_ENV", "APP_ENV", "ENV", "PYTHON_ENV"):
        value = (os.getenv(env_name) or "").strip().lower()
        if value in _PRODUCTION_ENV_VALUES:
            return True
    return False


def dev_signing_keys_allowed() -> bool:
    """Return True only when the built-in development signing key is opted into.

    The built-in key is derived from a constant string in this source tree, so
    its private half is public knowledge. Falling back to it implicitly (for
    example because an operator forgot to export
    ``INTENT_SERVER_ED25519_PRIVATE_KEY``) let anyone forge intent/operation
    mandates and tokens that this SDK would verify, because verification used
    the same local key instead of a configured trust anchor.

    Opting in requires either ``A4P_ALLOW_DEV_KEYS=1`` or an explicit
    non-production environment (``A4P_ENV`` / ``APP_ENV`` / ``ENV`` /
    ``PYTHON_ENV`` set to dev, development, local, test, testing or ci).

    Production always returns False, even with the opt-in set: the answer must
    not depend on the caller checking ``is_production_environment()`` first.
    The opt-in variable is compared against an explicit truthy set, so
    ``A4P_ALLOW_DEV_KEYS=0`` / ``false`` / ``off`` cannot accidentally enable
    the public built-in key.
    """
    if is_production_environment():
        return False
    if (os.getenv(_DEV_KEY_OPT_IN_VAR) or "").strip().lower() in _DEV_KEY_OPT_IN_VALUES:
        return True
    for env_name in ("A4P_ENV", "APP_ENV", "ENV", "PYTHON_ENV"):
        value = (os.getenv(env_name) or "").strip().lower()
        if value in _DEV_KEY_ENV_VALUES:
            return True
    return False


def ed25519_private_key_from_env(*, env_name: str, default_seed_label: str, purpose: str) -> Ed25519PrivateKey:
    raw = os.getenv(env_name)
    if raw is not None:
        value = raw.strip()
        if value:
            if value == default_seed_label:
                _reject_or_warn_default_key(purpose=purpose, env_name=env_name)
            return _load_ed25519_private_key(value, env_name=env_name)

    if is_production_environment():
        raise RuntimeError(
            f"{purpose} requires {env_name} in production mode; "
            "refusing to use the built-in development Ed25519 signing key."
        )
    if not dev_signing_keys_allowed():
        raise RuntimeError(
            f"{purpose} requires {env_name}; refusing to fall back to the built-in "
            f"development Ed25519 signing key. That key is derived from a constant in "
            f"the A4P source tree, so its private half is public and anyone could forge "
            f"signatures with it. Set {env_name} to a real secret, or set "
            f"{_DEV_KEY_OPT_IN_VAR}=1 for local development only."
        )
    _warn_default_key(purpose=purpose, env_name=env_name)
    return Ed25519PrivateKey.from_private_bytes(hashlib.sha256(default_seed_label.encode("utf-8")).digest())


def ed25519_sign_text(text: str, private_key: Ed25519PrivateKey) -> str:
    signature = private_key.sign(text.encode("utf-8"))
    return base64.urlsafe_b64encode(signature).decode("ascii").rstrip("=")


def ed25519_verify_text(text: str, signature: str, public_key: Ed25519PublicKey) -> bool:
    try:
        public_key.verify(_b64url_decode(signature), text.encode("utf-8"))
    except (InvalidSignature, ValueError, binascii.Error):
        return False
    return True


def ed25519_public_key_from_base64url(value: str) -> Ed25519PublicKey:
    """Load a raw 32-byte Ed25519 public key from unpadded base64url."""
    if not value or re.fullmatch(r"[A-Za-z0-9_-]+", value) is None:
        raise ValueError("Ed25519 public key must be valid base64url")
    try:
        raw = _b64url_decode(value)
    except (ValueError, binascii.Error) as exc:
        raise ValueError("Ed25519 public key must be valid base64url") from exc
    if len(raw) != 32:
        raise ValueError("Ed25519 public key must decode to 32 bytes")
    canonical = base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")
    if value != canonical:
        raise ValueError("Ed25519 public key must use unpadded canonical base64url")
    return Ed25519PublicKey.from_public_bytes(raw)


def ed25519_public_key_to_base64url(public_key: Ed25519PublicKey) -> str:
    """Serialize an Ed25519 public key as unpadded base64url."""
    raw = public_key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def _load_ed25519_private_key(value: str, *, env_name: str) -> Ed25519PrivateKey:
    if value.startswith("-----BEGIN"):
        try:
            loaded = serialization.load_pem_private_key(value.encode("utf-8"), password=None)
        except (TypeError, ValueError, UnsupportedAlgorithm) as exc:
            raise _invalid_private_key_error(env_name) from exc
        if not isinstance(loaded, Ed25519PrivateKey):
            raise RuntimeError(f"{env_name} must contain an Ed25519 private key")
        return loaded

    seed = _decode_seed(value, env_name=env_name)
    if len(seed) != 32:
        raise RuntimeError(f"{env_name} must decode to a 32-byte Ed25519 private key seed")
    return Ed25519PrivateKey.from_private_bytes(seed)


def _decode_seed(value: str, *, env_name: str) -> bytes:
    try:
        if value.startswith("hex:"):
            return bytes.fromhex(value[4:])
        if value.startswith("base64:"):
            return base64.b64decode(value[7:])
        if value.startswith("base64url:"):
            return _b64url_decode(value[10:])
        return _b64url_decode(value)
    except (ValueError, binascii.Error) as exc:
        raise _invalid_private_key_error(env_name) from exc


def _invalid_private_key_error(env_name: str) -> RuntimeError:
    return RuntimeError(
        f"{env_name} must be an Ed25519 PEM key or a 32-byte seed encoded as base64url, base64:, or hex:"
    )


def _b64url_decode(value: str) -> bytes:
    padding = "=" * (-len(value) % 4)
    return base64.urlsafe_b64decode((value + padding).encode("ascii"))


def _reject_or_warn_default_key(*, purpose: str, env_name: str) -> None:
    if is_production_environment():
        raise RuntimeError(
            f"{purpose} is configured with the built-in development Ed25519 signing key via {env_name}; "
            f"set {env_name} to a non-default secret."
        )
    _warn_default_key(purpose=purpose, env_name=env_name)


def _warn_default_key(*, purpose: str, env_name: str) -> None:
    if purpose in _WARNED_DEFAULT_KEYS:
        return
    _WARNED_DEFAULT_KEYS.add(purpose)
    logger.critical(
        "HIGH RISK: %s is using the built-in development signing key. "
        "Set %s before using A4P outside local development.",
        purpose,
        env_name,
    )
