"""Instance <-> heartbeat integration tests.

Verifies the bidirectional dependency declared in the dev plan:
- instance._derive_status calls heartbeat.is_expired(node)  (status derivation)
- heartbeat.sweeper calls instance.expire_node(node)         (grace-expired eviction)

Covers:
- set_heartbeat_service wires manager.is_expired as the status check
- registered instances derive 运行 when node is healthy
- registered instances derive 异常 when node lease goes UNHEALTHY
- instances recover to 运行 when node re-heartbeats (soft recovery)
- sweeper evicts all instances on a node when grace expires
- sweeper does not touch instances on healthy nodes
- set_heartbeat_service(None) resets to all-运行
"""

from __future__ import annotations

import time

import pytest

from a2x_registry.common.lease import LeaseState
from a2x_registry.common.db import connect, init_schema
from a2x_registry.common.ids import instance_sid
from a2x_registry.register.service import RegistryTableService
from a2x_registry.instance.service import InstanceService
from a2x_registry.instance.deps import set_instance_service
from a2x_registry.heartbeat.service import HeartbeatManager
from a2x_registry.heartbeat.store import NodeHeartbeatStore
from a2x_registry.heartbeat.sweeper import NodeHeartbeatSweeper


@pytest.fixture
def table_svc():
    backend = connect({"kind": "memory"})
    init_schema(backend.conn)
    svc = RegistryTableService(backend)
    svc.create_registry("instances", "instance")
    yield svc


@pytest.fixture
def instance_svc(table_svc):
    svc = InstanceService(table_svc)
    set_instance_service(svc)
    yield svc
    set_instance_service(None)


@pytest.fixture
def hb_manager():
    return HeartbeatManager(NodeHeartbeatStore())


def _make_entry(user="alice", framework="langchain", node="192.168.0.11", **kw):
    return {
        "service_id": instance_sid(user, framework),
        "kind": kw.get("kind", "三方"),
        "framework": framework,
        "framework_version": kw.get("framework_version", "0.2.0"),
        "node": node,
        "address": kw.get("address", "10.244.1.7:4096"),
        "user": user,
    }


# ── set_heartbeat_service wires is_expired ─────────────────────

def test_set_heartbeat_service_wires_status_check(instance_svc, hb_manager):
    """set_heartbeat_service(manager) -> _derive_status uses manager.is_expired."""
    instance_svc.set_heartbeat_service(hb_manager)
    instance_svc.register_instance(_make_entry(node="192.168.0.11"))
    # Node has no lease -> 运行
    assert instance_svc.list_instances()[0][0]["status"] == "运行"

    # Heartbeat the node -> still 运行
    hb_manager.heartbeat("192.168.0.11")
    assert instance_svc.list_instances()[0][0]["status"] == "运行"

    # Force node UNHEALTHY -> 异常
    lease = hb_manager.store.get_lease("192.168.0.11")
    lease.state = LeaseState.UNHEALTHY
    assert instance_svc.list_instances(include_unhealthy=True)[0][0]["status"] == "异常"


def test_set_heartbeat_service_none_resets_to_healthy(instance_svc, hb_manager):
    instance_svc.set_heartbeat_service(hb_manager)
    instance_svc.register_instance(_make_entry(node="192.168.0.11"))
    lease = hb_manager.store.get_lease("192.168.0.11")
    if lease is None:
        hb_manager.heartbeat("192.168.0.11")
        lease = hb_manager.store.get_lease("192.168.0.11")
    lease.state = LeaseState.UNHEALTHY
    assert instance_svc.list_instances(include_unhealthy=True)[0][0]["status"] == "异常"

    instance_svc.set_heartbeat_service(None)
    assert instance_svc.list_instances()[0][0]["status"] == "运行"


# ── soft recovery: heartbeat restores status ───────────────────

def test_node_re_heartbeat_restores_healthy_status(instance_svc, hb_manager):
    instance_svc.set_heartbeat_service(hb_manager)
    instance_svc.register_instance(_make_entry(node="192.168.0.11"))
    hb_manager.heartbeat("192.168.0.11")
    lease = hb_manager.store.get_lease("192.168.0.11")
    # Expire the lease
    lease.state = LeaseState.UNHEALTHY
    assert instance_svc.list_instances(include_unhealthy=True)[0][0]["status"] == "异常"

    # Re-heartbeat -> soft recovery
    hb_manager.heartbeat("192.168.0.11")
    assert instance_svc.list_instances()[0][0]["status"] == "运行"


# ── sweeper evicts instances on grace-expired nodes ────────────

def test_sweeper_evicts_all_instances_on_expired_node(instance_svc, hb_manager):
    """When a node's grace expires, sweeper calls expire_node -> instances deleted."""
    instance_svc.set_heartbeat_service(hb_manager)
    # Two instances on 192.168.0.11, one on 192.168.0.12
    instance_svc.register_instance(_make_entry(user="alice", framework="langchain", node="192.168.0.11"))
    instance_svc.register_instance(_make_entry(user="alice", framework="llama_index", node="192.168.0.11"))
    instance_svc.register_instance(_make_entry(user="bob", framework="langchain", node="192.168.0.12"))

    # Heartbeat both nodes, then let 192.168.0.11 expire past grace
    hb_manager.heartbeat("192.168.0.11")
    hb_manager.heartbeat("192.168.0.12")
    lease = hb_manager.store.get_lease("192.168.0.11")
    # Both nodes heartbeated at nearly the same instant; extend 192.168.0.12's
    # grace so only 192.168.0.11 is past grace at the sweep time.
    hb_manager.store.get_lease("192.168.0.12").grace_deadline = lease.grace_deadline + 100

    sweeper = NodeHeartbeatSweeper(hb_manager, instance_svc=instance_svc, period=60.0)
    sweeper.sweep_once(now=lease.grace_deadline + 0.01)

    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    nodes = {r["node"] for r in rows}
    # 192.168.0.11 instances evicted; 192.168.0.12 remains
    assert nodes == {"192.168.0.12"}
    assert len(rows) == 1


def test_sweeper_does_not_evict_healthy_node(instance_svc, hb_manager):
    instance_svc.set_heartbeat_service(hb_manager)
    instance_svc.register_instance(_make_entry(node="192.168.0.11"))
    hb_manager.heartbeat("192.168.0.11")
    lease = hb_manager.store.get_lease("192.168.0.11")

    sweeper = NodeHeartbeatSweeper(hb_manager, instance_svc=instance_svc, period=60.0)
    # Sweep before expiry -> nothing happens
    sweeper.sweep_once(now=lease.expires_at - 1)
    assert len(instance_svc.list_instances(include_unhealthy=True)[0]) == 1


# ── include_unhealthy filtering with real heartbeat ────────────

def test_list_excludes_unhealthy_by_default(instance_svc, hb_manager):
    instance_svc.set_heartbeat_service(hb_manager)
    instance_svc.register_instance(_make_entry(node="192.168.0.11"))
    instance_svc.register_instance(_make_entry(user="bob", framework="llama_index", node="192.168.0.12"))
    hb_manager.heartbeat("192.168.0.11")
    hb_manager.heartbeat("192.168.0.12")
    # Mark 192.168.0.11 unhealthy
    hb_manager.store.get_lease("192.168.0.11").state = LeaseState.UNHEALTHY

    # Default -> only 192.168.0.12
    rows, _ = instance_svc.list_instances()
    assert len(rows) == 1
    assert rows[0]["node"] == "192.168.0.12"

    # include_unhealthy -> both
    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    assert len(rows) == 2
