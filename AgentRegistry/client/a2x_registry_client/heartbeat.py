"""Client-side heartbeat renewal — daemon thread per (dataset, sid).

Used by ``A2XRegistryClient`` when the user registers with ``auto_renew=True``
and the server granted a lease. The renewer wakes every ``ttl/3`` seconds
(Eureka convention) and POSTs to the heartbeat endpoint. On failure it
backs off exponentially; if the lease will surely expire before the next
attempt, it surrenders (the caller's TTL eventually expires server-side —
fail-safe).

Design notes:
- One thread per (ds, sid). Lightweight: when the SDK manages 100 services,
  100 daemon threads, each idle ~99% of the time. Daemon=True so process
  exit doesn't hang.
- Backoff: start at ttl/3, double on each failure up to ttl (so we always
  attempt at least one renewal per nominal window before surrendering).
- All state behind ``threading.Event`` so ``stop()`` is prompt.
- No network call in the thread setup — start() just spawns; the first
  attempt happens after the first ``period`` wait. This avoids surprising
  delays in ``register_agent`` and matches Eureka's "first heartbeat at
  t + interval" behavior.
"""

from __future__ import annotations

import logging
import threading
import time
from typing import Callable, Dict, Optional, Tuple

logger = logging.getLogger(__name__)


# Callback signature: (dataset, sid) -> None; SDK injects a closure that
# does the actual POST through its transport. Keeps this module
# transport-agnostic (testable with a plain function).
HeartbeatFn = Callable[[str, str], None]


class HeartbeatRenewer:
    """Single-service heartbeat daemon."""

    def __init__(
        self,
        dataset: str,
        service_id: str,
        ttl_seconds: int,
        heartbeat_fn: HeartbeatFn,
        *,
        period: Optional[float] = None,
    ) -> None:
        if ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds}")
        self.dataset = dataset
        self.service_id = service_id
        self.ttl = int(ttl_seconds)
        self._fn = heartbeat_fn
        # Default period = ttl/3 (rounded down, min 1). Override is mostly
        # for tests that want millisecond-fast renewal.
        self._period = float(period) if period is not None else max(1.0, self.ttl / 3.0)
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        """Spawn the daemon. Idempotent — second call is a no-op."""
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run,
            name=f"HBRenewer({self.dataset}/{self.service_id})",
            daemon=True,
        )
        self._thread.start()

    def stop(self, timeout: float = 2.0) -> None:
        """Signal the daemon to exit. Idempotent."""
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=timeout)
            self._thread = None

    def _run(self) -> None:
        """Daemon loop with exponential backoff on failure (capped at ttl)."""
        cur_period = self._period
        while not self._stop.wait(cur_period):
            try:
                self._fn(self.dataset, self.service_id)
                # Success: reset to nominal period
                cur_period = self._period
            except Exception as exc:  # noqa: BLE001 — daemon must survive
                # Back off; if we'd be sleeping past the TTL, surrender.
                cur_period = min(cur_period * 2, float(self.ttl))
                logger.warning(
                    "heartbeat renewer for (%s, %s) failed; backing off to %ss: %s",
                    self.dataset, self.service_id, cur_period, exc,
                )
                if cur_period >= self.ttl:
                    logger.warning(
                        "heartbeat renewer for (%s, %s) surrendering — lease "
                        "will expire on the server. Caller should re-register "
                        "or rely on grace_period.",
                        self.dataset, self.service_id,
                    )
                    # Stay in the loop but at TTL period — gives the lease
                    # one last shot if the network comes back during grace.


class HeartbeatRegistry:
    """Per-client registry of all active renewers.

    Owned by ``A2XRegistryClient`` so ``close()`` can shutdown_all in one
    call. Keyed by (dataset, sid) so the same service registered twice
    replaces the old renewer cleanly.
    """

    def __init__(self) -> None:
        self._renewers: Dict[Tuple[str, str], HeartbeatRenewer] = {}
        self._lock = threading.Lock()

    def add(self, renewer: HeartbeatRenewer) -> None:
        key = (renewer.dataset, renewer.service_id)
        with self._lock:
            old = self._renewers.pop(key, None)
            self._renewers[key] = renewer
        if old is not None:
            old.stop()
        renewer.start()

    def remove(self, dataset: str, service_id: str) -> None:
        """Stop and forget the renewer for this sid. Idempotent."""
        with self._lock:
            renewer = self._renewers.pop((dataset, service_id), None)
        if renewer is not None:
            renewer.stop()

    def shutdown_all(self, timeout: float = 2.0) -> None:
        """Stop every renewer. Called from ``A2XRegistryClient.close()``."""
        with self._lock:
            renewers = list(self._renewers.values())
            self._renewers.clear()
        for r in renewers:
            r.stop(timeout=timeout)


# ─────────────────────────────────────────────────────────────────────────
# Async-native variant (used by AsyncA2XRegistryClient)
# ─────────────────────────────────────────────────────────────────────────


import asyncio as _asyncio  # noqa: E402 — keep below sync types to avoid
                              # asyncio import cost for sync-only users
from typing import Awaitable  # noqa: E402

AsyncHeartbeatFn = Callable[[str, str], Awaitable[None]]


class AsyncHeartbeatRenewer:
    """asyncio.Task-based renewer for AsyncA2XRegistryClient.

    Same semantics as the sync ``HeartbeatRenewer`` — wakes every ``ttl/3``,
    exponential backoff on failure capped at ttl. Difference: lifecycle
    methods are coroutines (``await renewer.stop()``), and the actual
    heartbeat call is an awaited coroutine.
    """

    def __init__(
        self,
        dataset: str,
        service_id: str,
        ttl_seconds: int,
        heartbeat_fn: AsyncHeartbeatFn,
        *,
        period: Optional[float] = None,
    ) -> None:
        if ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds}")
        self.dataset = dataset
        self.service_id = service_id
        self.ttl = int(ttl_seconds)
        self._fn = heartbeat_fn
        self._period = float(period) if period is not None else max(1.0, self.ttl / 3.0)
        self._task: Optional[_asyncio.Task] = None
        self._stop = _asyncio.Event()

    def start(self) -> None:
        """Schedule the renewer task on the current event loop."""
        if self._task is not None and not self._task.done():
            return
        self._stop.clear()
        self._task = _asyncio.create_task(
            self._run(),
            name=f"AsyncHBRenewer({self.dataset}/{self.service_id})",
        )

    async def stop(self) -> None:
        """Signal exit + await task completion. Idempotent."""
        self._stop.set()
        if self._task is not None:
            try:
                await self._task
            except _asyncio.CancelledError:
                pass
            self._task = None

    async def _run(self) -> None:
        cur_period = self._period
        try:
            while not self._stop.is_set():
                try:
                    await _asyncio.wait_for(self._stop.wait(), timeout=cur_period)
                    break  # stop event set
                except _asyncio.TimeoutError:
                    pass
                try:
                    await self._fn(self.dataset, self.service_id)
                    cur_period = self._period
                except Exception as exc:  # noqa: BLE001
                    cur_period = min(cur_period * 2, float(self.ttl))
                    logger.warning(
                        "async heartbeat renewer for (%s, %s) failed; "
                        "backing off to %ss: %s",
                        self.dataset, self.service_id, cur_period, exc,
                    )
        except _asyncio.CancelledError:
            raise


class AsyncHeartbeatRegistry:
    """asyncio-native registry of AsyncHeartbeatRenewer tasks. Same API
    shape as HeartbeatRegistry but ``shutdown_all`` is a coroutine."""

    def __init__(self) -> None:
        self._renewers: Dict[Tuple[str, str], AsyncHeartbeatRenewer] = {}

    def add(self, renewer: AsyncHeartbeatRenewer) -> None:
        key = (renewer.dataset, renewer.service_id)
        # Schedule stop of the old renewer if any — fire-and-forget to
        # avoid making add() async. Caller is single-event-loop.
        old = self._renewers.pop(key, None)
        if old is not None:
            _asyncio.create_task(old.stop())
        self._renewers[key] = renewer
        renewer.start()

    async def remove(self, dataset: str, service_id: str) -> None:
        renewer = self._renewers.pop((dataset, service_id), None)
        if renewer is not None:
            await renewer.stop()

    async def shutdown_all(self) -> None:
        renewers = list(self._renewers.values())
        self._renewers.clear()
        for r in renewers:
            await r.stop()
