"""Shared internal helpers for authorization services."""

from __future__ import annotations

from collections.abc import Callable, Mapping
from copy import deepcopy
from dataclasses import is_dataclass
from typing import Any

from a4p.types import to_payload


def payload(value: Any) -> dict[str, Any]:
    converted = to_payload(value) if is_dataclass(value) else value
    return converted if isinstance(converted, dict) else {}


def error_code(reason: str, prefix: str) -> str:
    lower = reason.lower()
    if "expired" in lower:
        return f"{prefix}_EXPIRED"
    if "usage" in lower or "execution" in lower or "limit" in lower:
        return f"{prefix}_USAGE_EXCEEDED"
    if "signature" in lower:
        return f"{prefix}_SIGNATURE_INVALID"
    if "mismatch" in lower or "scope" in lower or "actions" in lower or "param" in lower:
        return f"{prefix}_SCOPE_MISMATCH"
    return f"{prefix}_INVALID"


def mandate_matches_pending(
    signed_mandate: dict[str, Any],
    pending_mandate: dict[str, Any],
    *,
    normalize: Callable[[Mapping[str, Any]], Mapping[str, Any]],
) -> bool:
    try:
        signed = deepcopy(dict(normalize(signed_mandate)))
        pending = deepcopy(dict(normalize(pending_mandate)))
    except (TypeError, ValueError):
        return False
    signed["signatures"]["user"] = {}
    pending["signatures"]["user"] = {}
    return signed == pending
