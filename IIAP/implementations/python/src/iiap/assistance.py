"""Validation for request-scoped text assistance output."""

from __future__ import annotations

import re
from typing import Literal


AssistanceTextViolation = Literal["empty", "too_large", "a2ui_tag", "a2ui_message"]

_A2UI_KEY_PATTERN = re.compile(
    r'["\'](?:beginRendering|surfaceUpdate|dataModelUpdate|deleteSurface|'
    r'createSurface|updateComponents|updateDataModel)["\']\s*:',
)


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


__all__ = ["AssistanceTextViolation", "validate_assistance_text"]
