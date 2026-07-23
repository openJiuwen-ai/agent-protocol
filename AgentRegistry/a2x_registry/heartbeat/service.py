"""HeartbeatManager - per-node heartbeat service (appliance mode).

Thin orchestration layer over ``NodeHeartbeatStore``: exposes the
heartbeat / is_expired / recover_from_persisted surface for the router
and instance module, and drives the sweep -> on_expire callback chain.

The ``on_expire`` callback is optional (``None`` means sweep only marks
nodes disconnected without triggering instance eviction). In appliance
mode startup wires ``InstanceService.expire_node`` as ``on_expire`` so
that nodes past their grace window have all their instances deleted.

This module is FastAPI-free (testable without uvicorn). The router in
``heartbeat/router.py`` mounts the REST endpoints; ``heartbeat/deps.py``
holds the module-level singleton.
"""

from __future__ import annotations

import logging
from typing import Callable, Iterable, List, Optional

from a2x_registry.common.lease import Lease

from .store import NodeHeartbeatStore

logger = logging.getLogger(__name__)

# (node_ip) -> None; called when a node exceeds its grace window.
NodeExpireCallback = Callable[[str], None]


class HeartbeatManager:
    """Per-node heartbeat manager with injectable expire callback.

    Created once at backend startup (appliance mode). The store is owned
    by the manager; the optional ``on_expire`` callback is invoked from
    ``sweep_once`` for each node whose grace window has elapsed.
    """

    __slots__ = ("_store", "_on_expire")

    def __init__(
        self,
        store: NodeHeartbeatStore,
        on_expire: Optional[NodeExpireCallback] = None,
    ) -> None:
        self._store = store
        self._on_expire = on_expire

    @property
    def store(self) -> NodeHeartbeatStore:
        """The underlying per-node lease store."""
        return self._store

    def set_on_expire(self, callback: Optional[NodeExpireCallback]) -> None:
        """Inject (or clear) the grace-expired callback."""
        self._on_expire = callback

    # ── heartbeat / is_expired (router + instance facing) ───────

    def heartbeat(self, node: str) -> Lease:
        """Install or renew the node's lease (soft recovery from UNHEALTHY)."""
        return self._store.node_heartbeat(node)

    def is_expired(self, node: str) -> bool:
        """True if the node's lease is UNHEALTHY. Drives instance status."""
        return self._store.is_expired(node)

    def expired_nodes(self) -> set:
        """Return the set of node IPs currently UNHEALTHY (read-only).

        Used by instance query to push ``node NOT IN (...)` into SQL
        when ``include_unhealthy=False``.
        """
        return self._store.expired_nodes()

    # ── restart recovery ────────────────────────────────────────

    def recover_from_persisted(self, nodes: Iterable[str]) -> None:
        """Rebuild per-node leases from persisted instances after a restart."""
        self._store.recover_from_persisted(nodes)

    # ── sweep ───────────────────────────────────────────────────

    def sweep_once(self, now: Optional[float] = None) -> List[str]:
        """Run one sweep pass; invoke ``on_expire`` for each evicted node.

        Returns the list of evicted node IPs. When ``on_expire`` is None
        the sweep still removes expired leases from the store (nodes are
        marked disconnected) but no eviction callback fires - matching
        the design's ``on_expire`` optional semantic.

        Exceptions from ``on_expire`` are logged and swallowed so the
        sweeper daemon never crashes on a callback failure.
        """
        evicted = self._store.sweep_expired_nodes(now)
        if self._on_expire is not None and evicted:
            for node in evicted:
                try:
                    self._on_expire(node)
                except Exception as exc:  # noqa: BLE001 - sweeper must survive
                    logger.warning(
                        "node-heartbeat: on_expire failed for %s: %s", node, exc,
                    )
        return evicted
