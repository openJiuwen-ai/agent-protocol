"""Instance management domain exceptions.

Business-layer exceptions that carry no HTTP semantics. The router is
responsible for translating them into HTTP status codes:
- ``InstanceNotFoundError`` → 404
- ``InstanceValidationError`` → 400
"""

from __future__ import annotations

from a2x_registry.register.errors import NotFoundError, ValidationError


class InstanceNotFoundError(NotFoundError):
    """Instance (service_id) not found. Router maps to 404."""


class InstanceValidationError(ValidationError):
    """Instance input validation failed (missing field / invalid kind).
    Router maps to 400."""


__all__ = [
    "InstanceNotFoundError",
    "InstanceValidationError",
]
