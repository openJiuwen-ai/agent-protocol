"""Module-level ClusterStore singleton + FastAPI helpers.

Same pattern as ``auth/deps.py`` and ``heartbeat/deps.py``: a module
global the dependency layer can short-circuit on when ``None`` (= cluster
not initialized → routes 404, read-path merge is a no-op).
"""

from __future__ import annotations

from typing import Optional

from fastapi import HTTPException

from .store import ClusterStore

_store: Optional[ClusterStore] = None


def get_cluster_store() -> Optional[ClusterStore]:
    """Return the active ClusterStore, or ``None`` if not initialized."""
    return _store


def set_cluster_store(store: Optional[ClusterStore]) -> None:
    """Inject (or clear) the global ClusterStore. Called from backend
    startup and from test fixtures (reset to ``None`` between cases)."""
    global _store
    _store = store


def require_cluster_store() -> ClusterStore:
    """FastAPI dependency: 404 when the cluster module isn't initialized.

    404 (not 503) matches auth/heartbeat posture: from a non-cluster
    registry's perspective these routes simply don't exist. Run
    ``a2x-registry cluster init`` to enable them.
    """
    store = get_cluster_store()
    if store is None:
        raise HTTPException(
            status_code=404,
            detail=(
                "Cluster module not initialized on this registry. "
                "Run 'a2x-registry cluster init' to enable distributed sync."
            ),
        )
    return store
