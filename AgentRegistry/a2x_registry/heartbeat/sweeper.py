"""HeartbeatSweeper — background daemon that drives state transitions.

Single daemon thread, period configurable (default 5s). Each tick calls
``HeartbeatStore.sweep_tick()`` to get the lists of newly-unhealthy and
to-hard-delete sids, then invokes ``RegistryService.deregister`` for the
latter (using the synthetic ``SYSTEM_CTX`` — same code path as
admin-initiated DELETE, so cache invalidation / file rewrite / vector
sync all fire as expected).

Tests can either drive ``store.sweep_tick`` directly (synchronous,
millisecond-fast — preferred) or spin a sweeper with a short period.
``sweep_once()`` is exposed for the direct-drive case.
"""

from __future__ import annotations

import logging
import threading
import time
from typing import TYPE_CHECKING

from .errors import HeartbeatError  # noqa: F401  — re-exported via __init__
from .store import HeartbeatStore
from .system_ctx import SYSTEM_CTX

if TYPE_CHECKING:
    from a2x_registry.register.service import RegistryService

logger = logging.getLogger(__name__)


class HeartbeatSweeper:
    """Background daemon that runs the heartbeat state machine."""

    def __init__(
        self,
        registry_svc: "RegistryService",
        store: HeartbeatStore,
        period: float = 5.0,
    ) -> None:
        self._svc = registry_svc
        self._store = store
        self._period = float(period)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        """Spawn the daemon thread. Idempotent — second call is a no-op."""
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, name="HeartbeatSweeper", daemon=True,
        )
        self._thread.start()
        logger.info("heartbeat: sweeper started (period=%ss)", self._period)

    def stop(self, timeout: float = 2.0) -> None:
        """Signal the daemon to exit and wait briefly. Used in tests."""
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=timeout)
            self._thread = None

    def sweep_once(self) -> None:
        """Run one sweep pass. Exposed for tests to drive synchronously
        without spinning the daemon. Also called by ``_run`` each tick."""
        newly_unhealthy, to_hard_delete = self._store.sweep_tick()
        for dataset, sid in newly_unhealthy:
            logger.info(
                "heartbeat: service marked unhealthy (%s, %s)", dataset, sid,
            )
        for dataset, sid in to_hard_delete:
            self._hard_delete(dataset, sid)

    def _hard_delete(self, dataset: str, sid: str) -> None:
        """Invoke the same deregister path that an admin user would.

        Failures are logged but not re-raised — the lease is already gone
        from the store; worst case the entry becomes a permanent service
        until operator cleanup. We MUST NOT crash the sweeper here.
        """
        try:
            self._svc.deregister(dataset, sid, caller=SYSTEM_CTX)
            logger.info(
                "heartbeat: hard-deleted (%s, %s) after grace expired",
                dataset, sid,
            )
        except Exception as exc:  # noqa: BLE001 — sweeper must survive any error
            logger.warning(
                "heartbeat: hard-delete failed for (%s, %s): %s",
                dataset, sid, exc,
            )

    def _run(self) -> None:
        """Daemon loop. Uses ``Event.wait(timeout)`` so ``stop()`` is prompt."""
        while not self._stop.is_set():
            try:
                self.sweep_once()
            except Exception as exc:  # noqa: BLE001 — defensive
                logger.exception("heartbeat: sweep_once raised: %s", exc)
            # Sleep interruptibly so test teardown doesn't hang.
            self._stop.wait(self._period)
