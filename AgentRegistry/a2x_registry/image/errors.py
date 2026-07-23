"""Image management domain exceptions.

Business-layer exceptions that carry no HTTP semantics. The router is
responsible for translating them into HTTP status codes:
- ``ImageNotFoundError`` → 404
- ``ImageValidationError`` → 400
- ``ImageInUseError`` → 409 (deregister blocked by in-use instances)
- ``RepoDeleteError`` → 502 (image repository deletion failed)

``ImageInUseError`` / ``ExternalDependencyError`` are reused from
``register.errors`` to avoid duplication.
"""

from __future__ import annotations

from a2x_registry.register.errors import (
    ExternalDependencyError as RepoDeleteError,
    ImageInUseError,
    NotFoundError,
    ValidationError,
)


class ImageNotFoundError(NotFoundError):
    """Image (framework + version) not found. Router maps to 404."""


class ImageValidationError(ValidationError):
    """Image input validation failed (missing field / version conflict).
    Router maps to 400."""


__all__ = [
    "ImageNotFoundError",
    "ImageValidationError",
    "ImageInUseError",
    "RepoDeleteError",
]
