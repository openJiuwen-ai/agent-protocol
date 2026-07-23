"""HeartbeatStore - per-namespace lease tracking on top of the shared
``LeaseTable`` state machine.

Pure runtime store: no disk I/O, no FastAPI. The corresponding persisted
field ``RegistryEntry.lease_ttl`` lives in the register module. The store
is wired into ``RegistryService`` via the ``set_unhealthy_check`` callback
at backend startup; the sweeper invokes ``RegistryService.deregister``
through the synthetic ``SYSTEM_CTX`` for hard deletion.

This module owns the heartbeat-specific concerns - the per-namespace
4-corner validation matrix and the ``(dataset, service_id)`` keying -
and delegates the actual lease countdown / state transitions to
``a2x_registry.common.lease.LeaseTable``.

``NodeHeartbeatStore`` is the appliance-mode per-node variant (key =
node IP). It coexists with the per-service ``HeartbeatStore`` using a
separate ``LeaseTable`` instance so the two key spaces never mix.
"""

from __future__ import annotations

import logging
from typing import Iterable, List, Optional, Tuple

from a2x_registry.common.lease import Lease, LeaseState, LeaseTable

from .errors import (
    HeartbeatNotSupportedError,
    TTLOutOfRangeError,
    TTLRequiredError,
)
from .models import HeartbeatLease, NodeLeaseConfig

logger = logging.getLogger(__name__)


# (dataset, service_id) -> HeartbeatLease
_LeaseKey = Tuple[str, str]


class HeartbeatStore:
    """In-memory registry of active heartbeat leases.

    Created once at backend startup with a ``config_provider`` callable that
    yields per-namespace ``lease_config`` dicts (typically
    ``RegistryService.get_lease_config``). The store never reads disk on
    its own — it asks the registry service for current config on every
    grant, so admin toggles take effect immediately.
    """

    def __init__(self, config_provider) -> None:
        """``config_provider(dataset) -> dict`` returns the namespace's
        lease config (with keys ``enabled``, ``min_ttl``, ``max_ttl``,
        ``grace_period``). Indirection avoids coupling HeartbeatStore to
        RegistryService internals — tests can pass a lambda."""
        self._config_provider = config_provider
        self._table: LeaseTable[_LeaseKey] = LeaseTable()

    # ── validate / install / heartbeat / revoke ─────────────────────────

    def validate(
        self,
        dataset: str,
        client_ttl: Optional[int],
    ) -> Optional[int]:
        """Apply the 4-corner matrix; return the validated ttl or ``None``.

        Pure check — no state change, safe to call before registration.
        The router calls this upfront so that if validation fails, no
        partial state is created. On success, the caller then registers
        the service and invokes ``install()`` with the same ttl.

        Return value:
        - ``None``: namespace doesn't support heartbeat AND client didn't
          request one (permanent service, legacy path)
        - ``int``: validated ttl ready for ``install()``

        Raises ``HeartbeatNotSupportedError`` / ``TTLRequiredError`` /
        ``TTLOutOfRangeError`` for the rejection corners.
        """
        cfg = self._config_provider(dataset) or {}
        ns_enabled = bool(cfg.get("enabled", False))

        # Corner 1: ns ❌ / client ❌ → permanent
        if not ns_enabled and client_ttl is None:
            return None
        # Corner 2: ns ❌ / client ✅ → reject
        if not ns_enabled and client_ttl is not None:
            raise HeartbeatNotSupportedError(
                f"Namespace {dataset!r} does not enable heartbeat leases. "
                f"Remove 'lease_ttl' from the request or ask admin to enable it."
            )
        # Corner 3: ns ✅ / client ❌ → reject (strict)
        min_ttl = int(cfg.get("min_ttl", 10))
        max_ttl = int(cfg.get("max_ttl", 3600))
        if client_ttl is None:
            raise TTLRequiredError(
                f"Namespace {dataset!r} requires 'lease_ttl' on registration; "
                f"allowed range [{min_ttl}, {max_ttl}].",
                min_ttl=min_ttl, max_ttl=max_ttl,
            )
        # Corner 4: ns ✅ / client ✅ — bound check
        if not isinstance(client_ttl, int) or client_ttl < min_ttl or client_ttl > max_ttl:
            raise TTLOutOfRangeError(
                f"lease_ttl={client_ttl} out of range [{min_ttl}, {max_ttl}] "
                f"for namespace {dataset!r}.",
                min_ttl=min_ttl, max_ttl=max_ttl,
            )
        return int(client_ttl)

    def _grace(self, dataset: str) -> int:
        cfg = self._config_provider(dataset) or {}
        return int(cfg.get("grace_period", 300))

    def install(
        self,
        dataset: str,
        service_id: str,
        ttl: int,
    ) -> HeartbeatLease:
        """Install a pre-validated lease. ``ttl`` MUST have come from
        ``validate()`` — no bound checks here. Called after register
        succeeded so we have a real service_id.

        Idempotent: if a lease already exists for (dataset, sid), it's
        replaced. Returns the new lease for response composition.
        """
        lease = self._table.install((dataset, service_id), ttl, self._grace(dataset))
        logger.debug("heartbeat: installed (%s, %s) ttl=%s", dataset, service_id, ttl)
        return lease

    def grant(
        self,
        dataset: str,
        service_id: str,
        client_ttl: Optional[int],
    ) -> Optional[HeartbeatLease]:
        """Convenience wrapper: validate + install in one call.

        Useful for tests / scripts that don't need to interleave with
        ``RegistryService.register_*``. Production register path uses
        ``validate()`` and ``install()`` separately to avoid creating a
        dangling lease if register fails after validation.
        """
        validated = self.validate(dataset, client_ttl)
        if validated is None:
            return None
        return self.install(dataset, service_id, validated)

    def heartbeat(
        self,
        dataset: str,
        service_id: str,
    ) -> HeartbeatLease:
        """Extend the lease; HEALTHY recovery from UNHEALTHY also works
        (the design's 'soft eviction' semantic — heartbeat during grace
        period restores the service silently).

        Raises ``KeyError`` if no lease exists for (dataset, service_id) —
        the caller (router) maps that to 404.
        """
        try:
            lease = self._table.renew((dataset, service_id))
        except KeyError:
            raise KeyError(
                f"No heartbeat lease for service {service_id!r} in dataset "
                f"{dataset!r} — was it registered with lease_ttl?"
            )
        logger.debug("heartbeat: extended (%s, %s)", dataset, service_id)
        return lease

    def revoke(
        self,
        dataset: str,
        service_id: str,
        *,
        permanent: bool = False,
    ) -> bool:
        """Mark a lease unhealthy (default) or hard-drop it (``permanent=True``).

        Default ``revoke`` puts the lease into UNHEALTHY immediately but
        leaves it in the table so a heartbeat during the (restarted) grace
        window can recover. ``permanent=True`` removes the lease entirely;
        the caller is expected to also call ``RegistryService.deregister``
        to remove the entry.

        Returns ``True`` if a lease was found and acted on, ``False`` if
        no lease existed (idempotent). Never raises.
        """
        found = self._table.revoke((dataset, service_id), permanent=permanent)
        if found:
            logger.debug(
                "heartbeat: revoked (%s, %s) permanent=%s",
                dataset, service_id, permanent,
            )
        return found

    def drop(self, dataset: str, service_id: str) -> None:
        """Remove the lease entry without touching state. Used after
        ``RegistryService.deregister`` (e.g. operator-initiated DELETE)
        so the heartbeat store doesn't keep a stale lease for a dead sid.
        Idempotent."""
        self._table.drop((dataset, service_id))

    # ── read paths (called by router + register_service callback) ───────

    def is_unhealthy(self, dataset: str, service_id: str) -> bool:
        """The callback installed via ``RegistryService.set_unhealthy_check``.

        Returns True only when a lease exists AND its state is UNHEALTHY.
        Services with no lease (permanent / no heartbeat) → always False
        (healthy by definition; nothing to monitor).
        """
        return self._table.is_unhealthy((dataset, service_id))

    def get_lease(self, dataset: str, service_id: str) -> Optional[HeartbeatLease]:
        """Snapshot read for routers / tests. Returns the dataclass by
        reference; callers must not mutate."""
        return self._table.get((dataset, service_id))

    def list_leases(self) -> List[Tuple[str, str, HeartbeatLease]]:
        """All (dataset, sid, lease) triples — used by tests / debug."""
        return [(ds, sid, lease) for (ds, sid), lease in self._table.items()]

    # ── sweep (called by HeartbeatSweeper daemon) ───────────────────────

    def sweep_tick(self, now: Optional[float] = None) -> Tuple[List[_LeaseKey], List[_LeaseKey]]:
        """Single sweep pass — state transitions, returns work for caller.

        Returns ``(newly_unhealthy, to_hard_delete)``. The caller (sweeper)
        is responsible for invoking ``RegistryService.deregister`` for
        each to_hard_delete sid; the lease table itself only updates the
        in-memory state. Separation lets us test the state machine without
        mocking RegistryService.

        ``now`` defaults to ``time.monotonic()`` — overridable for tests
        that want to drive time forward without sleep.
        """
        return self._table.sweep_tick(now)

    # ── restart recovery ───────────────────────────────────────────────

    def recover_from_persisted(
        self, entries: List[Tuple[str, str, int]],
    ) -> None:
        """Re-grant grace-window leases for entries persisted with lease_ttl.

        Called once at startup by the sweeper with the list of
        ``(dataset, sid, lease_ttl)`` triples from every dataset's
        api_config.json. Each gets a lease in UNHEALTHY state with
        ``grace_deadline = now + grace_period`` — clients have one grace
        window to send a heartbeat after the registry comes back. Without
        this, restart would silently drop heartbeat protection for every
        previously-registered service.
        """
        for dataset, sid, ttl in entries:
            self._table.install(
                (dataset, sid), int(ttl), self._grace(dataset), expired=True,
            )
        logger.info(
            "heartbeat: recovered %d leases from disk into grace window", len(entries),
        )


class NodeHeartbeatStore:
    """Per-node lease store for appliance mode (key = node IP).

    Coexists with the per-service ``HeartbeatStore`` (A2X). Uses its own
    ``LeaseTable`` instance keyed by ``str`` (node IP) so the two key
    spaces never mix. The gateway heartbeats once per node, covering all
    instances on that node; ``is_expired`` drives instance status
    derivation, and ``sweep_expired_nodes`` drives grace-expired eviction.

    State machine (per node):
        HEALTHY --TTL elapsed--> UNHEALTHY + grace window
        UNHEALTHY --heartbeat within grace--> HEALTHY (soft recovery)
        UNHEALTHY --grace elapsed--> disconnected -> on_expire deletes instances
    """

    def __init__(self, config: Optional[NodeLeaseConfig] = None) -> None:
        self._config = config if config is not None else NodeLeaseConfig()
        self._table: LeaseTable[str] = LeaseTable()

    # ── config ──────────────────────────────────────────────────

    def get_config(self) -> NodeLeaseConfig:
        """Return the current lease config (mutable; callers must not abuse)."""
        return self._config

    def update_config(self, **fields: object) -> None:
        """Mutate config fields in-place. Unknown keys are ignored so the
        router can forward the full OpenAPI body without pre-filtering."""
        for key in ("enabled", "min_ttl", "max_ttl", "grace_period"):
            if key in fields:
                setattr(self._config, key, fields[key])
        logger.info(
            "node-heartbeat: config updated (enabled=%s, min_ttl=%s, grace=%s)",
            self._config.enabled, self._config.min_ttl, self._config.grace_period,
        )

    # ── heartbeat / is_expired ──────────────────────────────────

    def node_heartbeat(self, node: str) -> Lease:
        """Install (first beat) or renew (subsequent) the node's lease.

        First heartbeat installs a HEALTHY lease. Subsequent heartbeats
        renew via ``LeaseTable.renew``, which also performs soft recovery
        from UNHEALTHY within the grace window.

        Raises ``HeartbeatNotSupportedError`` when the config has
        ``enabled=False``.
        """
        if not self._config.enabled:
            raise HeartbeatNotSupportedError(
                "Node heartbeat leases are disabled (lease-config.enabled=false)."
            )
        existing = self._table.get(node)
        if existing is None:
            lease = self._table.install(
                node, self._config.min_ttl, self._config.grace_period,
            )
        else:
            lease = self._table.renew(node)
        logger.debug("node-heartbeat: renewed %s", node)
        return lease

    def is_expired(self, node: str) -> bool:
        """True if a lease exists AND its state is UNHEALTHY.

        Missing lease -> False (covers the pre-first-heartbeat window
        where the instance was just registered but the gateway hasn't
        sent a heartbeat yet). After restart recovery every node with
        instances gets an UNHEALTHY+grace lease, so ``is_expired`` is
        True until the gateway re-heartbeats.
        """
        return self._table.is_unhealthy(node)

    def get_lease(self, node: str) -> Optional[Lease]:
        """Snapshot read for routers / tests."""
        return self._table.get(node)

    def list_nodes(self) -> List[Tuple[str, Lease]]:
        """All ``(node, lease)`` pairs - for tests / debug."""
        return self._table.items()

    def expired_nodes(self) -> set:
        """Return the set of node IPs that are currently UNHEALTHY.

        Read-only: does not modify leases. Used by instance query to
        push ``node NOT IN (...)`` into SQL when ``include_unhealthy=False``,
        so filtering and pagination both happen in the database.
        """
        return {node for node, lease in self._table.items()
                if lease.state == LeaseState.UNHEALTHY}

    # ── sweep ───────────────────────────────────────────────────

    def sweep_expired_nodes(
        self, now: Optional[float] = None,
    ) -> List[str]:
        """Single sweep pass; returns nodes whose grace window elapsed.

        The returned nodes have their leases removed from the table. The
        caller (``HeartbeatManager.sweep_once``) invokes ``on_expire`` for
        each, which typically calls ``InstanceService.expire_node`` to
        delete all instances on that node.
        """
        _newly_unhealthy, to_delete = self._table.sweep_tick(now)
        return to_delete

    # ── restart recovery ────────────────────────────────────────

    def recover_from_persisted(self, nodes: Iterable[str]) -> None:
        """Seed UNHEALTHY + grace leases for nodes that have instances.

        Called once at startup with ``InstanceService.distinct_nodes()``.
        Each node gets a lease in UNHEALTHY state with
        ``grace_deadline = now + grace_period`` - the gateway has one
        grace window to re-heartbeat after a registry restart. If it
        doesn't, the sweeper evicts all instances on that node.
        """
        node_list = list(nodes)
        for node in node_list:
            self._table.install(
                node, self._config.min_ttl, self._config.grace_period,
                expired=True,
            )
        logger.info(
            "node-heartbeat: recovered %d nodes into grace window", len(node_list),
        )
