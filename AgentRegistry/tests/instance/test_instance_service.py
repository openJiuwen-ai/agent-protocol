"""InstanceService 业务逻辑测试（memory 后端）。

覆盖全部函数契约：
- register_instance：新增 / 幂等 upsert 保留 created_at / 校验
- update_instance：部分更新 node/address / 404 / 空字段
- deregister_instance：删存在 / 幂等删不存在
- list_instances：全量 / 过滤 / include_unhealthy
- expire_node：删该 node 全部实例 / 幂等 / 未知 node
- _derive_status：未注入心跳 → 运行；注入 → 异常
- distinct_nodes：去重排序（供重启恢复）
"""

from __future__ import annotations

import pytest

from a2x_registry.instance.errors import (
    InstanceNotFoundError,
    InstanceValidationError,
)
from a2x_registry.instance.service import InstanceService

from .conftest import make_entry


# ── register_instance ──────────────────────────────────────────

def test_register_new_instance(instance_svc: InstanceService):
    """首次注册 → status=运行，全部字段落库。"""
    entry = make_entry()
    result = instance_svc.register_instance(entry)
    assert result["service_id"] == entry["service_id"]
    assert result["kind"] == "三方"
    assert result["framework"] == "langchain"
    assert result["framework_version"] == "0.2.0"
    assert result["node"] == "192.168.0.11"
    assert result["address"] == "10.244.1.7:4096"
    assert result["user"] == "alice"
    assert result["status"] == "运行"
    # created_at / last_active_at 均已填写
    assert result["created_at"]
    assert result["last_active_at"]
    assert result["created_at"] == result["last_active_at"]


def test_register_upsert_preserves_created_at(instance_svc: InstanceService):
    """同 service_id 二次注册 → created_at 保留，last_active_at 更新，
    node/address 覆盖。"""
    r1 = instance_svc.register_instance(
        make_entry(node="192.168.0.11", address="http://old")
    )
    created_at = r1["created_at"]

    r2 = instance_svc.register_instance(
        make_entry(node="192.168.0.99", address="http://new")
    )
    assert r2["created_at"] == created_at          # 保留
    assert r2["node"] == "192.168.0.99"            # 覆盖
    assert r2["address"] == "http://new"           # 覆盖
    # last_active_at 已刷新（至少不早于 created_at）
    assert r2["last_active_at"] >= r2["created_at"]


def test_register_invalid_kind_rejected(instance_svc: InstanceService):
    """kind 只接受 三方 / 九问。"""
    with pytest.raises(InstanceValidationError, match="kind"):
        instance_svc.register_instance(make_entry(kind="xxx"))


def test_register_missing_field_rejected(instance_svc: InstanceService):
    """缺少必填字段 → ValidationError。"""
    entry = make_entry()
    del entry["node"]
    with pytest.raises(InstanceValidationError, match="node"):
        instance_svc.register_instance(entry)


# ── update_instance ────────────────────────────────────────────

def test_update_node_only(instance_svc: InstanceService):
    """PATCH 只改 node → node 更新，address 不动。"""
    instance_svc.register_instance(make_entry(node="10.0.0.1", address="http://old"))
    sid = make_entry()["service_id"]
    result = instance_svc.update_instance(sid, {"node": "10.0.0.2"})
    assert result["node"] == "10.0.0.2"
    assert result["address"] == "http://old"       # 保留


def test_update_address_only(instance_svc: InstanceService):
    """PATCH 只改 address → address 更新，last_active_at 刷新。"""
    r1 = instance_svc.register_instance(make_entry(address="http://old"))
    result = instance_svc.update_instance(r1["service_id"], {"address": "http://new"})
    assert result["address"] == "http://new"
    assert result["last_active_at"] >= r1["last_active_at"]


def test_update_both_node_and_address(instance_svc: InstanceService):
    """PATCH 同时改 node + address。"""
    instance_svc.register_instance(make_entry())
    sid = make_entry()["service_id"]
    result = instance_svc.update_instance(
        sid, {"node": "10.0.0.9", "address": "10.9.9.9:8080"}
    )
    assert result["node"] == "10.0.0.9"
    assert result["address"] == "10.9.9.9:8080"


def test_update_missing_instance_raises_404(instance_svc: InstanceService):
    """更新不存在的 sid → InstanceNotFoundError。"""
    with pytest.raises(InstanceNotFoundError):
        instance_svc.update_instance("generic_nope", {"node": "10.0.0.1"})


def test_update_no_fields_rejected(instance_svc: InstanceService):
    """空 fields（既无 node 也无 address）→ ValidationError。"""
    instance_svc.register_instance(make_entry())
    sid = make_entry()["service_id"]
    with pytest.raises(InstanceValidationError):
        instance_svc.update_instance(sid, {})


# ── deregister_instance ────────────────────────────────────────

def test_deregister_existing(instance_svc: InstanceService):
    """删存在的实例 → deleted=True。"""
    instance_svc.register_instance(make_entry())
    sid = make_entry()["service_id"]
    result = instance_svc.deregister_instance(sid)
    assert result == {"service_id": sid, "deleted": True}
    # 再查 -> 不在列表中
    rows, _ = instance_svc.list_instances()
    assert rows == []


def test_deregister_missing_is_idempotent(instance_svc: InstanceService):
    """删不存在的实例 → deleted=False（幂等）。"""
    result = instance_svc.deregister_instance("generic_nope")
    assert result == {"service_id": "generic_nope", "deleted": False}


# ── list_instances ─────────────────────────────────────────────

def _seed_list_data(instance_svc: InstanceService):
    """灌入 3 条实例供过滤测试。"""
    instance_svc.register_instance(
        make_entry(user="alice", framework="langchain", node="192.168.0.11", kind="三方")
    )
    instance_svc.register_instance(
        make_entry(user="alice", framework="llama_index", node="192.168.0.11", kind="三方")
    )
    instance_svc.register_instance(
        make_entry(user="bob", framework="langchain", node="192.168.0.12", kind="九问")
    )


def test_list_all(instance_svc: InstanceService):
    _seed_list_data(instance_svc)
    rows, _ = instance_svc.list_instances()
    assert len(rows) == 3
    assert all(r["status"] == "运行" for r in rows)


def test_list_filter_by_node(instance_svc: InstanceService):
    _seed_list_data(instance_svc)
    rows, _ = instance_svc.list_instances(filter={"node": "192.168.0.11"})
    assert len(rows) == 2


def test_list_filter_by_framework(instance_svc: InstanceService):
    _seed_list_data(instance_svc)
    rows, _ = instance_svc.list_instances(filter={"framework": "langchain"})
    assert len(rows) == 2


def test_list_filter_by_kind(instance_svc: InstanceService):
    _seed_list_data(instance_svc)
    rows, _ = instance_svc.list_instances(filter={"kind": "九问"})
    assert len(rows) == 1
    assert rows[0]["user"] == "bob"


def test_list_filter_by_user(instance_svc: InstanceService):
    _seed_list_data(instance_svc)
    rows, _ = instance_svc.list_instances(filter={"user": "alice"})
    assert len(rows) == 2


def test_list_exclude_unhealthy_by_default(instance_svc: InstanceService):
    """include_unhealthy=False（默认）→ 异常实例被过滤掉。"""
    _seed_list_data(instance_svc)
    # 标记 192.168.0.11 为异常
    instance_svc.set_heartbeat_check(lambda node: node == "192.168.0.11")
    rows, _ = instance_svc.list_instances()
    # 192.168.0.11 的 2 条被过滤，只剩 192.168.0.12 的 1 条
    assert len(rows) == 1
    assert rows[0]["node"] == "192.168.0.12"


def test_list_include_unhealthy(instance_svc: InstanceService):
    """include_unhealthy=True → 全部返回（含异常）。"""
    _seed_list_data(instance_svc)
    instance_svc.set_heartbeat_check(lambda node: node == "192.168.0.11")
    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    assert len(rows) == 3
    statuses = {r["status"] for r in rows}
    assert statuses == {"运行", "异常"}


# ── _derive_status ─────────────────────────────────────────────

def test_status_defaults_healthy(instance_svc: InstanceService):
    """未注入心跳 → 全部 运行。"""
    instance_svc.register_instance(make_entry())
    rows, _ = instance_svc.list_instances()
    assert rows[0]["status"] == "运行"


def test_status_derived_from_heartbeat(instance_svc: InstanceService):
    """注入心跳：node 在 expired 集合中 → 异常。"""
    instance_svc.register_instance(make_entry(node="192.168.0.11"))
    instance_svc.register_instance(
        make_entry(user="bob", framework="llama_index", node="192.168.0.12")
    )
    expired = {"192.168.0.11"}
    instance_svc.set_heartbeat_check(lambda node: node in expired)

    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    by_node = {r["node"]: r["status"] for r in rows}
    assert by_node["192.168.0.11"] == "异常"
    assert by_node["192.168.0.12"] == "运行"


def test_status_reset_when_heartbeat_cleared(instance_svc: InstanceService):
    """清除心跳注入后 → 恢复 运行。"""
    instance_svc.register_instance(make_entry(node="192.168.0.11"))
    instance_svc.set_heartbeat_check(lambda node: node == "192.168.0.11")
    assert instance_svc.list_instances(include_unhealthy=True)[0][0]["status"] == "异常"

    instance_svc.set_heartbeat_check(None)
    assert instance_svc.list_instances()[0][0]["status"] == "运行"


# ── expire_node ────────────────────────────────────────────────

def test_expire_node_deletes_all(instance_svc: InstanceService):
    """expire_node 删该 node 全部实例，其他 node 不受影响。"""
    _seed_list_data(instance_svc)
    instance_svc.expire_node("192.168.0.11")
    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    # 只剩 192.168.0.12 的 bob
    assert len(rows) == 1
    assert rows[0]["node"] == "192.168.0.12"


def test_expire_node_idempotent(instance_svc: InstanceService):
    """二次 expire 同一 node → 无副作用。"""
    _seed_list_data(instance_svc)
    instance_svc.expire_node("192.168.0.11")
    instance_svc.expire_node("192.168.0.11")          # 二次
    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    assert len(rows) == 1


def test_expire_node_unknown_noop(instance_svc: InstanceService):
    """expire 不存在的 node → 无异常，表无变化。"""
    _seed_list_data(instance_svc)
    instance_svc.expire_node("10.99.99.99")
    rows, _ = instance_svc.list_instances(include_unhealthy=True)
    assert len(rows) == 3


# ── distinct_nodes ─────────────────────────────────────────────

def test_distinct_nodes(instance_svc: InstanceService):
    """distinct_nodes 返回去重 + 排序的 node 列表。"""
    _seed_list_data(instance_svc)
    nodes = instance_svc.distinct_nodes()
    assert nodes == ["192.168.0.11", "192.168.0.12"]


def test_distinct_nodes_empty(instance_svc: InstanceService):
    """无实例时返回空列表。"""
    assert instance_svc.distinct_nodes() == []
