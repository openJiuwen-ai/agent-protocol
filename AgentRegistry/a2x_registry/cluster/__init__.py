"""Distributed sync module for A2X Registry.

Opt-in: a registry instance runs standalone until ``a2x-registry cluster
init`` creates ``cluster_state.json``. Once initialized, instances connect
into a full mesh (every member a direct peer) and sync their flat registry
(CRUD/list/get/filter) so a query to any instance returns every member's
services; a member that goes silent is dropped by its peers' HOLD timers,
which evict its records.

Design model (see ``docs/cluster_design.md``):
  - full mesh: each member has a direct session with every other member,
    maintained by the declarative membership control plane (``membership.py``)
    — users run ``cluster set add/remove``; the roster drives the mesh.
  - AP / eventually-consistent: a local CRUD broadcasts directly to all
    peers (no relay); periodic Merkle anti-entropy heals dropped pushes.
  - LWW versioning; origin-only writes; external records are read-only,
    memory-only replicas.
  - liveness = direct keepalive / HOLD; a HOLD-evicted origin is suppressed
    briefly so anti-entropy can't resurrect it before every peer evicts.
  - scale (~1000 nodes): concurrent fan-out, pooled HTTP, Merkle anti-entropy.

Public API:
    - ``ClusterStore`` — the single stateful object for this instance
    - ``MembershipStore`` — the membership control plane (attached to the store)
    - ``ClusterState`` / ``ClusterConfig`` — persisted state / tuning
    - ``get_cluster_store`` / ``set_cluster_store`` — module singleton hooks
    - ``router`` — FastAPI router for ``/api/cluster/*``
"""

from .config import ClusterConfig
from .state import ClusterState
from .store import ClusterStore
from .membership import MembershipStore
from .deps import get_cluster_store, set_cluster_store
from .router import router

__all__ = [
    "ClusterConfig",
    "ClusterState",
    "ClusterStore",
    "MembershipStore",
    "get_cluster_store",
    "set_cluster_store",
    "router",
]
