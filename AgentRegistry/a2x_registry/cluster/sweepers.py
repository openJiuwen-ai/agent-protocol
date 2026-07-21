"""Background daemons for the cluster module.

``AntiEntropySweeper`` periodically reconciles with each peer (healing any
push that was dropped while a link was flaky) and GCs expired tombstones.
Mirrors ``HeartbeatSweeper``'s defensive structure: a single daemon thread,
each tick wrapped so one error never kills the loop.

``tick()`` is exposed so tests drive it synchronously (no sleep).
"""

from __future__ import annotations

import logging
import threading

from .transport import TransportError

logger = logging.getLogger(__name__)


class AntiEntropySweeper:
    """Periodic reconcile + tombstone GC."""

    def __init__(self, store, period: float = 20.0) -> None:
        self._store = store
        self._period = float(period)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def tick(self) -> None:
        """One pass: reconcile membership (connect/disconnect to match the
        roster), reconcile each peer's records + membership deltas
        (best-effort), GC tombstones, and prune suppression entries."""
        self._reconcile_membership()
        for peer in self._store.list_peers():
            try:
                self._store.reconcile(peer)
                if self._store.membership is not None:
                    self._store.membership.reconcile_with(peer)
            except TransportError:
                pass  # peer unreachable now; a later tick will catch up
            except Exception as exc:  # noqa: BLE001 — never kill the loop
                logger.warning("cluster: anti-entropy reconcile with %s failed: %s",
                               peer.node_id, exc)
        # Memory hygiene — guarded so a blip here can't skip the rest.
        try:
            self._store.gc_tombstones()
            self._store.prune_suppression()
            if self._store.membership is not None:
                self._store.membership.gc_membership()
        except Exception as exc:  # noqa: BLE001 — never kill the loop
            logger.warning("cluster: GC pass failed: %s", exc)

    def _reconcile_membership(self) -> None:
        """Drive the session set toward the declarative roster (membership
        owns the policy)."""
        m = getattr(self._store, "membership", None)
        if m is not None:
            m.reconcile_connections()

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, name="ClusterAntiEntropy", daemon=True,
        )
        self._thread.start()
        logger.info("cluster: anti-entropy sweeper started (period=%ss)", self._period)

    def stop(self, timeout: float = 2.0) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=timeout)
            self._thread = None

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self.tick()
            except Exception as exc:  # noqa: BLE001 — defensive
                logger.exception("cluster: anti-entropy tick raised: %s", exc)
            self._stop.wait(self._period)


class KeepaliveMonitor:
    """Sends direct-link keepalives and drops peers past their HOLD timer."""

    def __init__(self, store, period: float = 10.0) -> None:
        self._store = store
        self._period = float(period)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def tick(self) -> None:
        self._store.emit_keepalive()
        self._store.check_hold()

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, name="ClusterKeepalive", daemon=True,
        )
        self._thread.start()
        logger.info("cluster: keepalive monitor started (period=%ss)", self._period)

    def stop(self, timeout: float = 2.0) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=timeout)
            self._thread = None

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self.tick()
            except Exception as exc:  # noqa: BLE001 — defensive
                logger.exception("cluster: keepalive tick raised: %s", exc)
            self._stop.wait(self._period)
