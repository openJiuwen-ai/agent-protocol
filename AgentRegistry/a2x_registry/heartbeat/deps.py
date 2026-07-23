"""Module-level HeartbeatStore singleton + FastAPI dep helpers.

Mirrors ``a2x_registry.auth.deps`` shape exactly - single global, set
once at backend startup, returned by ``get_heartbeat_store()``. The
``None`` return path means "heartbeat module not initialized" - routers
should fall back to permanent-service behavior in that case (matches
the auth module's ``store is None -> anon`` fallback).

``get_node_heartbeat_manager`` / ``set_node_heartbeat_manager`` are the
appliance-mode per-node equivalents: the per-node ``HeartbeatManager``
is injected at startup (appliance mode only) and returned by the dep
helper. ``None`` means the per-node heartbeat module is not assembled
(non-appliance mode); the node heartbeat router returns 404 in that case.
"""

from __future__ import annotations

import logging
from typing import Optional

from .service import HeartbeatManager
from .store import HeartbeatStore

logger = logging.getLogger(__name__)

_store: Optional[HeartbeatStore] = None
_node_manager: Optional[HeartbeatManager] = None


def get_heartbeat_store() -> Optional[HeartbeatStore]:
    """Return the active HeartbeatStore, or ``None`` if not initialized."""
    return _store


def set_heartbeat_store(store: Optional[HeartbeatStore]) -> None:
    """Inject (or clear) the global HeartbeatStore. Called from backend
    startup + from test fixtures. Tests must reset to ``None`` between
    cases that exercise different store configurations."""
    global _store
    _store = store


def get_node_heartbeat_manager() -> Optional[HeartbeatManager]:
    """Return the per-node HeartbeatManager, or ``None`` if not assembled."""
    return _node_manager


def set_node_heartbeat_manager(manager: Optional[HeartbeatManager]) -> None:
    """Inject (or clear) the per-node HeartbeatManager. Called by startup
    assembly (appliance mode) and test fixtures."""
    global _node_manager
    _node_manager = manager
