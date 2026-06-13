"""HeartbeatStore — per-namespace lease tracking on top of the shared
``LeaseTable`` state machine.

Pure runtime store: no disk I/O, no FastAPI. The corresponding persisted
field ``RegistryEntry.lease_ttl`` lives in the register module. The store
is wired into ``RegistryService`` via the ``set_unhealthy_check`` callback
at backend startup; the sweeper invokes ``RegistryService.deregister``
through the synthetic ``SYSTEM_CTX`` for hard deletion.

This module owns the heartbeat-specific concerns — the per-namespace
4-corner validation matrix and the ``(dataset, service_id)`` keying —
and delegates the actual lease countdown / state transitions to
``a2x_registry.common.lease.LeaseTable``.
"""

from __future__ import annotations

import logging
from typing import List, Optional, Tuple

from a2x_registry.common.lease import LeaseTable

from .errors import (
    HeartbeatNotSupportedError,
    TTLOutOfRangeError,
    TTLRequiredError,
)
from .models import HeartbeatLease

logger = logging.getLogger(__name__)


# (dataset, service_id) → HeartbeatLease
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
