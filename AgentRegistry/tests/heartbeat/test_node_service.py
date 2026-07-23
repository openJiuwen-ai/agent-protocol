"""HeartbeatManager + NodeHeartbeatSweeper tests (appliance mode).

Covers the service / sweeper contract:
- HeartbeatManager.heartbeat / is_expired / recover_from_persisted delegate to store
- HeartbeatManager.sweep_once calls on_expire for each evicted node
- HeartbeatManager with on_expire=None only marks disconnected (no eviction)
- NodeHeartbeatSweeper.sweep_once drives the manager
- NodeHeartbeatSweeper wires instance_svc.expire_node as on_expire on init
- NodeHeartbeatSweeper daemon start/stop lifecycle
- on_expire exceptions are swallowed (sweeper must survive)
"""

from __future__ import annotations

import time

import pytest

from a2x_registry.common.lease import LeaseState
from a2x_registry.heartbeat.service import HeartbeatManager
from a2x_registry.heartbeat.store import NodeHeartbeatStore
from a2x_registry.heartbeat.sweeper import NodeHeartbeatSweeper


# ── HeartbeatManager delegation ────────────────────────────────

def test_manager_heartbeat_delegates_to_store():
    mgr = HeartbeatManager(NodeHeartbeatStore())
    lease = mgr.heartbeat("10.0.0.1")
    assert lease.state == LeaseState.HEALTHY
    assert mgr.is_expired("10.0.0.1") is False


def test_manager_is_expired_missing_node():
    mgr = HeartbeatManager(NodeHeartbeatStore())
    assert mgr.is_expired("10.99.99.99") is False


def test_manager_recover_from_persisted():
    mgr = HeartbeatManager(NodeHeartbeatStore())
    mgr.recover_from_persisted(["10.0.0.1", "10.0.0.2"])
    assert mgr.is_expired("10.0.0.1") is True
    assert mgr.is_expired("10.0.0.2") is True


# ── HeartbeatManager.sweep_once with on_expire ─────────────────

def test_sweep_once_calls_on_expire_for_evicted_nodes():
    calls = []
    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store, on_expire=calls.append)
    lease = store.node_heartbeat("10.0.0.1")
    evicted = mgr.sweep_once(now=lease.grace_deadline + 0.01)
    assert evicted == ["10.0.0.1"]
    assert calls == ["10.0.0.1"]


def test_sweep_once_no_on_expire_only_marks_disconnected():
    """on_expire=None -> sweep returns evicted nodes but no callback fires."""
    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store, on_expire=None)
    lease = store.node_heartbeat("10.0.0.1")
    evicted = mgr.sweep_once(now=lease.grace_deadline + 0.01)
    assert evicted == ["10.0.0.1"]
    # Lease still removed from store by sweep_tick
    assert store.get_lease("10.0.0.1") is None


def test_sweep_once_no_leases_noop():
    mgr = HeartbeatManager(NodeHeartbeatStore())
    assert mgr.sweep_once() == []


def test_sweep_once_survives_on_expire_exception():
    """If on_expire raises, sweep_once must not crash - log and continue."""

    def _bad_callback(node):
        raise RuntimeError("boom")

    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store, on_expire=_bad_callback)
    lease = store.node_heartbeat("10.0.0.1")
    # Must not raise
    evicted = mgr.sweep_once(now=lease.grace_deadline + 0.01)
    assert evicted == ["10.0.0.1"]


def test_set_on_expire_replaces_callback():
    calls1 = []
    calls2 = []
    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store, on_expire=calls1.append)

    l1 = store.node_heartbeat("10.0.0.1")
    mgr.sweep_once(now=l1.grace_deadline + 0.01)
    assert calls1 == ["10.0.0.1"]

    mgr.set_on_expire(calls2.append)
    l2 = store.node_heartbeat("10.0.0.2")
    mgr.sweep_once(now=l2.grace_deadline + 0.01)
    assert calls2 == ["10.0.0.2"]
    assert calls1 == ["10.0.0.1"]  # old callback not called


# ── NodeHeartbeatSweeper ───────────────────────────────────────

class _FakeInstanceService:
    """Minimal stub matching InstanceService.expire_node signature."""

    def __init__(self):
        self.expired_nodes = []

    def expire_node(self, node: str) -> None:
        self.expired_nodes.append(node)


def test_sweeper_wires_instance_expire_node_as_on_expire():
    """NodeHeartbeatSweeper(manager, instance_svc) sets on_expire."""
    inst = _FakeInstanceService()
    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store)
    sweeper = NodeHeartbeatSweeper(mgr, instance_svc=inst, period=60.0)

    lease = store.node_heartbeat("10.0.0.1")
    sweeper.sweep_once(now=lease.grace_deadline + 0.01)
    assert inst.expired_nodes == ["10.0.0.1"]


def test_sweeper_without_instance_svc_does_not_evict():
    """instance_svc=None -> sweep only marks disconnected."""
    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store)
    sweeper = NodeHeartbeatSweeper(mgr, instance_svc=None, period=60.0)

    lease = store.node_heartbeat("10.0.0.1")
    sweeper.sweep_once(now=lease.grace_deadline + 0.01)
    # Lease removed but no eviction callback
    assert store.get_lease("10.0.0.1") is None


def test_sweeper_start_stop_lifecycle():
    """Daemon thread starts and stops cleanly."""
    inst = _FakeInstanceService()
    store = NodeHeartbeatStore()
    mgr = HeartbeatManager(store)
    sweeper = NodeHeartbeatSweeper(mgr, instance_svc=inst, period=0.05)
    sweeper.start()
    # Idempotent start
    sweeper.start()
    assert sweeper.is_alive()
    sweeper.stop(timeout=1.0)
    assert not sweeper.is_alive()


def test_sweeper_stop_is_idempotent():
    mgr = HeartbeatManager(NodeHeartbeatStore())
    sweeper = NodeHeartbeatSweeper(mgr, period=60.0)
    sweeper.stop()  # never started
    sweeper.stop()  # double stop OK
