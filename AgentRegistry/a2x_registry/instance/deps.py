"""Module-level InstanceService singleton + FastAPI dependency injection.

Mirrors the ``a2x_registry.image.deps`` structure: a single global
variable injected at startup and returned by ``get_instance_service()``.
``None`` means the instance module is not assembled (non-appliance mode);
the router returns 404 in that case, matching the fallback semantics of
an uninitialized heartbeat module.
"""

from __future__ import annotations

from typing import Optional

from .service import InstanceService

_service: Optional[InstanceService] = None


def get_instance_service() -> Optional[InstanceService]:
    """Return the assembled InstanceService, or ``None`` if not assembled."""
    return _service


def set_instance_service(service: Optional[InstanceService]) -> None:
    """Inject (or clear) the global InstanceService. Called by startup
    assembly and test fixtures."""
    global _service
    _service = service
