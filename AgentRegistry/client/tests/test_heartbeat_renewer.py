"""HeartbeatRenewer — daemon thread lifecycle + backoff."""

from __future__ import annotations

import threading
import time

import pytest

from a2x_registry_client.heartbeat import HeartbeatRenewer, HeartbeatRegistry


def test_renewer_calls_fn_at_period():
    """Tight period + sleep — verify the fn is invoked at least N times."""
    calls = []
    lock = threading.Lock()

    def fn(ds, sid):
        with lock:
            calls.append((ds, sid, time.monotonic()))

    r = HeartbeatRenewer("ds", "sid", ttl_seconds=3, heartbeat_fn=fn, period=0.05)
    r.start()
    time.sleep(0.25)  # expect ~5 calls
    r.stop()
    assert len(calls) >= 3, f"expected at least 3 calls in 0.25s with period=0.05, got {len(calls)}"


def test_renewer_idempotent_start_stop():
    """start() twice should be a no-op; stop() before start() should be safe."""
    r = HeartbeatRenewer("ds", "sid", ttl_seconds=3, heartbeat_fn=lambda d, s: None, period=0.05)
    r.stop()  # before start — should not raise
    r.start()
    r.start()  # second start no-op
    r.stop()
    r.stop()  # double stop


def test_renewer_thread_is_daemon():
    """Daemon threads die on interpreter exit — production must not hang."""
    r = HeartbeatRenewer("ds", "sid", ttl_seconds=3, heartbeat_fn=lambda d, s: None, period=0.05)
    r.start()
    assert r._thread.daemon is True
    r.stop()


def test_renewer_backoff_on_failure():
    """If fn raises, renewer keeps trying with exponential backoff."""
    attempts = []

    def fn(ds, sid):
        attempts.append(time.monotonic())
        raise RuntimeError("simulated network failure")

    r = HeartbeatRenewer("ds", "sid", ttl_seconds=4, heartbeat_fn=fn, period=0.05)
    r.start()
    time.sleep(0.5)
    r.stop()
    assert len(attempts) >= 2, "renewer should keep retrying after failure"


def test_renewer_rejects_bad_ttl():
    with pytest.raises(ValueError):
        HeartbeatRenewer("ds", "sid", ttl_seconds=0, heartbeat_fn=lambda d, s: None)


def test_registry_replaces_existing_renewer():
    """Re-registering the same (ds, sid) stops the old one."""
    reg = HeartbeatRegistry()
    r1 = HeartbeatRenewer("ds", "sid", ttl_seconds=5, heartbeat_fn=lambda d, s: None, period=0.1)
    r2 = HeartbeatRenewer("ds", "sid", ttl_seconds=5, heartbeat_fn=lambda d, s: None, period=0.1)
    reg.add(r1)
    reg.add(r2)
    # r1 should be stopped, r2 should be the active one
    assert r1._stop.is_set()
    assert not r2._stop.is_set()
    reg.shutdown_all()


def test_registry_shutdown_all_stops_everyone():
    reg = HeartbeatRegistry()
    renewers = [
        HeartbeatRenewer("ds", f"sid{i}", ttl_seconds=5, heartbeat_fn=lambda d, s: None, period=0.1)
        for i in range(5)
    ]
    for r in renewers:
        reg.add(r)
    reg.shutdown_all()
    for r in renewers:
        assert r._stop.is_set()
