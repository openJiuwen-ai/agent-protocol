"""Module-level ImageService singleton + FastAPI dependency injection.

Mirrors the ``a2x_registry.heartbeat.deps`` structure: a single global
variable injected at startup and returned by ``get_image_service()``.
``None`` means the image module is not assembled (non-appliance mode);
the router returns 404 in that case, matching the fallback semantics of
an uninitialized heartbeat module.
"""

from __future__ import annotations

from typing import Optional

from .service import ImageService

_service: Optional[ImageService] = None


def get_image_service() -> Optional[ImageService]:
    """Return the assembled ImageService, or ``None`` if not assembled."""
    return _service


def set_image_service(service: Optional[ImageService]) -> None:
    """Inject (or clear) the global ImageService. Called by startup
    assembly and test fixtures."""
    global _service
    _service = service
