"""Module-level HeartbeatStore singleton + FastAPI dep helpers.

Mirrors ``a2x_registry.auth.deps`` shape exactly - single global, set
once at backend startup, returned by ``get_heartbeat_store()``. The
``None`` return path means "heartbeat module not initialized" - routers
should fall back to permanent-service behavior in that case (matches
the auth module's ``store is None -> anon`` fallback).

NOTE: the per-node ``HeartbeatManager`` singleton was removed together
with the appliance-mode node heartbeat endpoints.
"""

from __future__ import annotations

import logging
from typing import Optional

from .store import HeartbeatStore

logger = logging.getLogger(__name__)

_store: Optional[HeartbeatStore] = None


def get_heartbeat_store() -> Optional[HeartbeatStore]:
    """Return the active HeartbeatStore, or ``None`` if not initialized."""
    return _store


def set_heartbeat_store(store: Optional[HeartbeatStore]) -> None:
    """Inject (or clear) the global HeartbeatStore. Called from backend
    startup + from test fixtures. Tests must reset to ``None`` between
    cases that exercise different store configurations."""
    global _store
    _store = store
