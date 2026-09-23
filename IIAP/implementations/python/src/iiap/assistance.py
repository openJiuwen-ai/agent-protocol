"""Validation for request-scoped text assistance output."""

from __future__ import annotations

import json
import re
from functools import lru_cache
from importlib.resources import files
from typing import Any, Literal

from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError


AssistanceTextViolation = Literal["empty", "too_large", "a2ui_tag", "a2ui_message"]

_A2UI_KEY_PATTERN = re.compile(
    r'["\'](?:beginRendering|surfaceUpdate|dataModelUpdate|deleteSurface|'
    r'createSurface|updateComponents|updateDataModel)["\']\s*:',
)


@lru_cache(maxsize=1)
def _assistance_validator() -> Draft202012Validator:
    resource = files("iiap.schemas").joinpath("assistance.schema.json")
    return Draft202012Validator(json.loads(resource.read_text(encoding="utf-8")))


def validate_assistance_request(request: Any) -> None:
    """Validate a request or raise the stable, content-free contract error."""
    try:
        json.dumps(request, allow_nan=False)
        _assistance_validator().validate(request)
    except (ValidationError, TypeError, ValueError, RecursionError) as exc:
        raise ValueError("INVALID_ASSISTANCE_REQUEST") from exc


def validate_assistance_text(message: object) -> tuple[bool, AssistanceTextViolation | None]:
    """Return whether a model message is safe to present as text assistance."""
    if not isinstance(message, str) or not message.strip():
        return False, "empty"
    if len(message) > 4096:
        return False, "too_large"
    if re.search(r"<\s*a2ui-json\b", message, re.IGNORECASE):
        return False, "a2ui_tag"
    if _A2UI_KEY_PATTERN.search(message):
        return False, "a2ui_message"
    return True, None


__all__ = ["AssistanceTextViolation", "validate_assistance_request", "validate_assistance_text"]
