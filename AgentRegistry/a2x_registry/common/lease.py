"""Generic TTL lease state machine — shared by heartbeat and cluster modules.

A ``LeaseTable[K]`` tracks per-key countdown leases through a two-stage
expiry: ``HEALTHY`` → (TTL elapsed) → ``UNHEALTHY`` → (grace elapsed) →
hard-delete. It is the key-agnostic core extracted from the heartbeat
module so that:

  - ``heartbeat`` keys leases by ``(dataset, service_id)`` and, on hard
    delete, deregisters the service;
  - ``cluster`` keys liveness leases by ``origin_id`` (node id) and, on
    hard delete, evicts every replicated record from that origin.

The table only owns the **pure state transitions**. The TTL/grace values
are supplied by the caller on ``install`` (so each module keeps its own
config source), and the hard-delete *action* is performed by the caller's
sweeper from the ``to_delete`` list ``sweep_tick`` returns.

All time fields use ``time.monotonic()`` to be immune to wall-clock jumps
(NTP adjustments / suspended VM). Thread-safety: every mutation is behind
``self._lock``; each critical section is dict-lookup-fast.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from enum import Enum
from typing import Dict, Generic, List, Optional, Tuple, TypeVar

K = TypeVar("K")


class LeaseState(str, Enum):
    """Lease health state. ``str`` mixin → serializes cleanly to JSON."""

    HEALTHY = "healthy"      # renewed recently; expires_at is in the future
    UNHEALTHY = "unhealthy"  # TTL elapsed but still in grace window; can recover


@dataclass
class Lease:
    """Per-key lease countdown state.

    All time fields are ``time.monotonic()`` values. The wall-clock
    ``expires_at`` for client-facing responses is computed on demand by
    the caller (``now_wall + (expires_at - now_monotonic)``).
    """

    ttl_seconds: int                   # renewal interval the holder committed to
    grace_period_seconds: int          # extra window after TTL before hard delete
    expires_at: float                  # monotonic: when state flips HEALTHY → UNHEALTHY
    grace_deadline: float              # monotonic: when state flips UNHEALTHY → hard-delete
    last_renew_at: float               # monotonic: for audit / observability
    state: LeaseState = LeaseState.HEALTHY


class LeaseTable(Generic[K]):
    """In-memory table of active leases keyed by an opaque ``K``."""

    def __init__(self) -> None:
        self._leases: Dict[K, Lease] = {}
        self._lock = threading.Lock()

    # ── install / renew / revoke / drop ─────────────────────────────────

    def install(
        self,
        key: K,
        ttl: int,
        grace: int,
        *,
        expired: bool = False,
        now: Optional[float] = None,
    ) -> Lease:
        """Create (or replace) a lease for ``key``. Idempotent.

        ``expired=True`` seeds the lease directly in ``UNHEALTHY`` with the
        grace window already running (``expires_at = now``). Used for
        restart recovery, where the holder gets one grace window to renew
        before hard delete.
        """
        if now is None:
            now = time.monotonic()
        if expired:
            lease = Lease(
                ttl_seconds=int(ttl),
                grace_period_seconds=int(grace),
                expires_at=now,
                grace_deadline=now + grace,
                last_renew_at=now,
                state=LeaseState.UNHEALTHY,
            )
        else:
            lease = Lease(
                ttl_seconds=int(ttl),
                grace_period_seconds=int(grace),
                expires_at=now + ttl,
                grace_deadline=now + ttl + grace,
                last_renew_at=now,
                state=LeaseState.HEALTHY,
            )
        with self._lock:
            self._leases[key] = lease
        return lease

    def renew(self, key: K, *, now: Optional[float] = None) -> Lease:
        """Extend the lease and restore HEALTHY (recovers from UNHEALTHY
        within the grace window — the "soft eviction" semantic).

        Raises ``KeyError`` if no lease exists for ``key``.
        """
        if now is None:
            now = time.monotonic()
        with self._lock:
            lease = self._leases.get(key)
            if lease is None:
                raise KeyError(key)
            lease.last_renew_at = now
            lease.expires_at = now + lease.ttl_seconds
            lease.grace_deadline = lease.expires_at + lease.grace_period_seconds
            lease.state = LeaseState.HEALTHY
        return lease

    def revoke(
        self,
        key: K,
        *,
        permanent: bool = False,
        now: Optional[float] = None,
    ) -> bool:
        """Mark a lease ``UNHEALTHY`` (default) or hard-drop it
        (``permanent=True``).

        Default revoke flips to ``UNHEALTHY`` immediately and restarts the
        grace window from now, leaving the lease in the table so a renew
        during the window can still recover it. Returns ``True`` if a lease
        was found, ``False`` otherwise (idempotent). Never raises.
        """
        if now is None:
            now = time.monotonic()
        with self._lock:
            lease = self._leases.get(key)
            if lease is None:
                return False
            if permanent:
                del self._leases[key]
            else:
                lease.state = LeaseState.UNHEALTHY
                lease.expires_at = now
                lease.grace_deadline = now + lease.grace_period_seconds
        return True

    def drop(self, key: K) -> None:
        """Remove the lease without touching state. Idempotent."""
        with self._lock:
            self._leases.pop(key, None)

    # ── reads ───────────────────────────────────────────────────────────

    def get(self, key: K) -> Optional[Lease]:
        """Snapshot read. The returned dataclass must not be mutated."""
        with self._lock:
            return self._leases.get(key)

    def is_unhealthy(self, key: K) -> bool:
        """True only when a lease exists AND its state is ``UNHEALTHY``.
        Missing key → False (nothing to monitor)."""
        with self._lock:
            lease = self._leases.get(key)
        return lease is not None and lease.state == LeaseState.UNHEALTHY

    def items(self) -> List[Tuple[K, Lease]]:
        """All ``(key, lease)`` pairs — for listing / debug."""
        with self._lock:
            return list(self._leases.items())

    # ── sweep ───────────────────────────────────────────────────────────

    def sweep_tick(
        self, now: Optional[float] = None,
    ) -> Tuple[List[K], List[K]]:
        """Single sweep pass — runs the state machine, returns work.

        Returns ``(newly_unhealthy, to_delete)``. The caller performs the
        actual hard-delete side effects for each ``to_delete`` key; this
        table only removes the expired entries from its own dict. Keeping
        the side effect outside lets the state machine be unit-tested in
        isolation.

        ``now`` defaults to ``time.monotonic()`` — overridable so tests can
        drive time forward without sleeping.
        """
        if now is None:
            now = time.monotonic()
        newly_unhealthy: List[K] = []
        to_delete: List[K] = []
        with self._lock:
            for key, lease in self._leases.items():
                if lease.state == LeaseState.HEALTHY and now >= lease.expires_at:
                    lease.state = LeaseState.UNHEALTHY
                    newly_unhealthy.append(key)
                if lease.state == LeaseState.UNHEALTHY and now >= lease.grace_deadline:
                    to_delete.append(key)
            for key in to_delete:
                del self._leases[key]
        return newly_unhealthy, to_delete
