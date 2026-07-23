"""NodeHeartbeatStore - per-node lease store tests (appliance mode).

Covers the per-node lease contract:
- node_heartbeat: first beat installs HEALTHY, subsequent renews (soft recovery)
- is_expired: True only when lease exists AND UNHEALTHY; missing -> False
- recover_from_persisted: seeds UNHEALTHY + grace window for restart recovery
- sweep_expired_nodes: returns nodes whose grace window elapsed
- config: update_config mutates ttl / grace in-place
- enabled=False: node_heartbeat rejects (HeartbeatNotSupportedError)
"""

from __future__ import annotations

import time

import pytest

from a2x_registry.common.lease import LeaseState
from a2x_registry.heartbeat.errors import HeartbeatNotSupportedError
from a2x_registry.heartbeat.models import NodeLeaseConfig
from a2x_registry.heartbeat.store import NodeHeartbeatStore


# ── node_heartbeat ─────────────────────────────────────────────

def test_first_heartbeat_installs_healthy():
    store = NodeHeartbeatStore()
    lease = store.node_heartbeat("192.168.0.11")
    assert lease.state == LeaseState.HEALTHY
    assert lease.ttl_seconds == NodeLeaseConfig().min_ttl
    assert lease.grace_period_seconds == NodeLeaseConfig().grace_period
    # is_expired False right after install
    assert store.is_expired("192.168.0.11") is False


def test_subsequent_heartbeat_renews():
    store = NodeHeartbeatStore()
    l1 = store.node_heartbeat("10.0.0.1")
    # Force expiry then renew -> soft recovery
    l1.expires_at = time.monotonic() - 1
    l1.state = LeaseState.UNHEALTHY
    assert store.is_expired("10.0.0.1") is True

    l2 = store.node_heartbeat("10.0.0.1")
    assert l2.state == LeaseState.HEALTHY
    assert store.is_expired("10.0.0.1") is False


def test_heartbeat_different_nodes_independent():
    store = NodeHeartbeatStore()
    store.node_heartbeat("10.0.0.1")
    store.node_heartbeat("10.0.0.2")
    nodes = dict(store.list_nodes())
    assert set(nodes.keys()) == {"10.0.0.1", "10.0.0.2"}


# ── is_expired ─────────────────────────────────────────────────

def test_is_expired_missing_node_returns_false():
    """No lease -> not expired (covers pre-first-heartbeat window)."""
    store = NodeHeartbeatStore()
    assert store.is_expired("10.99.99.99") is False


def test_is_expired_true_when_unhealthy():
    store = NodeHeartbeatStore()
    lease = store.node_heartbeat("10.0.0.1")
    lease.state = LeaseState.UNHEALTHY
    assert store.is_expired("10.0.0.1") is True


# ── recover_from_persisted ─────────────────────────────────────

def test_recover_from_persisted_seeds_unhealthy_grace():
    """Restart recovery: each node gets UNHEALTHY + grace window."""
    store = NodeHeartbeatStore()
    store.recover_from_persisted(["10.0.0.1", "10.0.0.2"])
    for node in ("10.0.0.1", "10.0.0.2"):
        lease = store.get_lease(node)
        assert lease is not None
        assert lease.state == LeaseState.UNHEALTHY
        assert store.is_expired(node) is True


def test_recover_from_persisted_empty_list_noop():
    store = NodeHeartbeatStore()
    store.recover_from_persisted([])
    assert store.list_nodes() == []


def test_recover_then_heartbeat_restores_healthy():
    """After restart recovery, a heartbeat soft-recovers to HEALTHY."""
    store = NodeHeartbeatStore()
    store.recover_from_persisted(["10.0.0.1"])
    assert store.is_expired("10.0.0.1") is True

    lease = store.node_heartbeat("10.0.0.1")
    assert lease.state == LeaseState.HEALTHY
    assert store.is_expired("10.0.0.1") is False


# ── sweep_expired_nodes ────────────────────────────────────────

def test_sweep_healthy_node_not_returned():
    store = NodeHeartbeatStore()
    lease = store.node_heartbeat("10.0.0.1")
    expired = store.sweep_expired_nodes(now=lease.expires_at - 1)
    assert expired == []


def test_sweep_unhealthy_within_grace_not_returned():
    store = NodeHeartbeatStore()
    lease = store.node_heartbeat("10.0.0.1")
    # Past TTL but within grace
    now = lease.expires_at + 0.01
    assert now < lease.grace_deadline
    expired = store.sweep_expired_nodes(now=now)
    # Node transitioned to UNHEALTHY but not yet past grace -> not evicted
    assert expired == []
    assert store.is_expired("10.0.0.1") is True


def test_sweep_past_grace_returns_node_and_removes_lease():
    store = NodeHeartbeatStore()
    lease = store.node_heartbeat("10.0.0.1")
    expired = store.sweep_expired_nodes(now=lease.grace_deadline + 0.01)
    assert expired == ["10.0.0.1"]
    # Lease removed from store
    assert store.get_lease("10.0.0.1") is None


def test_sweep_multiple_nodes_only_expired_returned():
    store = NodeHeartbeatStore()
    l1 = store.node_heartbeat("10.0.0.1")
    l2 = store.node_heartbeat("10.0.0.2")
    # Both installed at nearly the same monotonic instant; extend l2's
    # grace deadline so only 10.0.0.1 is past grace at the sweep time.
    l2.grace_deadline = l1.grace_deadline + 100
    expired = store.sweep_expired_nodes(now=l1.grace_deadline + 0.01)
    assert expired == ["10.0.0.1"]
    # 10.0.0.2 still has a lease
    assert store.get_lease("10.0.0.2") is not None


def test_sweep_empty_store_noop():
    store = NodeHeartbeatStore()
    assert store.sweep_expired_nodes() == []


# ── config ─────────────────────────────────────────────────────

def test_default_config_values():
    cfg = NodeLeaseConfig()
    assert cfg.enabled is True
    assert cfg.min_ttl > 0
    assert cfg.grace_period > 0
    assert cfg.max_ttl >= cfg.min_ttl


def test_update_config_changes_ttl_and_grace():
    store = NodeHeartbeatStore()
    store.update_config(min_ttl=120, grace_period=60)
    lease = store.node_heartbeat("10.0.0.1")
    assert lease.ttl_seconds == 120
    assert lease.grace_period_seconds == 60


def test_disabled_config_rejects_heartbeat():
    store = NodeHeartbeatStore()
    store.update_config(enabled=False)
    with pytest.raises(HeartbeatNotSupportedError):
        store.node_heartbeat("10.0.0.1")


def test_get_config_reflects_updates():
    store = NodeHeartbeatStore()
    store.update_config(min_ttl=200, grace_period=45)
    cfg = store.get_config()
    assert cfg.min_ttl == 200
    assert cfg.grace_period == 45


# ── get_lease ──────────────────────────────────────────────────

def test_get_lease_returns_none_for_missing():
    store = NodeHeartbeatStore()
    assert store.get_lease("10.99.99.99") is None


def test_get_lease_returns_lease_for_existing():
    store = NodeHeartbeatStore()
    store.node_heartbeat("10.0.0.1")
    lease = store.get_lease("10.0.0.1")
    assert lease is not None
    assert lease.state == LeaseState.HEALTHY
