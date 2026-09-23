"""Fail-closed privacy checks shared by IIAP services."""

from __future__ import annotations

import json
from typing import Any

FORBIDDEN_KEYS = frozenset({
    "rawvalue", "rawtext", "rawevents", "rawevent", "valuehash", "texthash",
    "password", "passwd", "passcode", "secret", "authtoken", "accesstoken", "refreshtoken",
    "apikey", "authorization", "credential", "credentials", "cookie", "sessioncookie",
})


def _normalized_key(key: object) -> str:
    return "".join(character for character in str(key).lower() if character.isalnum())


def contains_forbidden_key(value: Any) -> bool:
    if isinstance(value, list):
        return any(contains_forbidden_key(item) for item in value)
    if not isinstance(value, dict):
        return False
    return any(
        _normalized_key(key) in FORBIDDEN_KEYS or contains_forbidden_key(child)
        for key, child in value.items()
    )


def validate_privacy(value: Any, *, max_bytes: int = 32768) -> bool:
    if contains_forbidden_key(value):
        return False
    try:
        size = len(json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode())
    except (TypeError, ValueError):
        return False
    return size <= max_bytes
