"""Security helpers for A4P signing configuration."""

from __future__ import annotations

import logging
import os


logger = logging.getLogger(__name__)

_PRODUCTION_ENV_VALUES = {"prod", "production"}
_WARNED_DEFAULT_KEYS: set[str] = set()


def is_production_environment() -> bool:
    for env_name in ("A4P_ENV", "APP_ENV", "ENV", "PYTHON_ENV"):
        value = (os.getenv(env_name) or "").strip().lower()
        if value in _PRODUCTION_ENV_VALUES:
            return True
    return False


def signing_key_from_env(*, env_name: str, default: str, purpose: str) -> str:
    raw = os.getenv(env_name)
    if raw is not None:
        key = raw.strip()
        if key:
            if key == default:
                _reject_or_warn_default_key(purpose=purpose, env_name=env_name)
            return key

    if is_production_environment():
        raise RuntimeError(
            f"{purpose} requires {env_name} in production mode; "
            "refusing to use the built-in development signing key."
        )
    _warn_default_key(purpose=purpose, env_name=env_name)
    return default


def _reject_or_warn_default_key(*, purpose: str, env_name: str) -> None:
    if is_production_environment():
        raise RuntimeError(
            f"{purpose} is configured with the built-in development signing key via {env_name}; "
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
