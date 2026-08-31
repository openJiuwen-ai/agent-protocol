"""instance/ 模块的原生 SQL 模式验证。

`instance/service.py` 的函数契约：
- register_instance(entry)          —— 幂等 upsert，service_id = instance_sid(user, framework)
- update_instance(sid, fields)     —— 部分更新 node/address/instance_id/status（不存在 → 404）
- deregister_instance(sid)          —— 幂等删
- list_instances(filter, include_unhealthy) —— 按 node/framework/kind/user 过滤
- status 落库 data JSON（data.status，gateway 写入），instance 表无 status 列

service_id 确定性派生：instance_sid(user, framework) = "generic_" + sha256(user|framework)[:8]
"""

from __future__ import annotations

import hashlib
import json

INS_REG = "instances"
NOW = "2026-07-13T10:00:00Z"


# ── service_id 确定性派生 ────────────────────────────────────

def test_instance_sid_deterministic():
    """instance_sid(user, framework) = "generic_" + sha256(user|framework)[:8]。

    同 (user, framework) 永远产出同一 service_id → 单例。
    """
    def instance_sid(user, framework):
        return "generic_" + hashlib.sha256(f"{user}|{framework}".encode()).hexdigest()[:8]

    sid1 = instance_sid("alice", "langchain")
    sid2 = instance_sid("alice", "langchain")
    sid3 = instance_sid("alice", "llama_index")
    sid4 = instance_sid("bob", "langchain")

    assert sid1 == sid2                              # 同入同出
    assert sid1 != sid3                              # 框架不同 → 不同
    assert sid1 != sid4                              # 用户不同 → 不同
    assert sid1.startswith("generic_")
    assert len(sid1) == len("generic_") + 8


# ── register_instance：幂等 upsert ───────────────────────────

def _register_instance(conn, sid, user="alice", fw="langchain", fw_ver="0.2.0",
                       node="192.168.0.11", kind="三方", address="http://x"):
    conn.execute(
        """
        INSERT INTO instance(registry, service_id, kind, framework, framework_version,
                             node, "user", data)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(registry, service_id) DO UPDATE SET
            kind=excluded.kind,
            framework=excluded.framework,
            framework_version=excluded.framework_version,
            node=excluded.node,
            data=excluded.data
        """,
        (INS_REG, sid, kind, fw, fw_ver, node, user,
         json.dumps({"address": address, "created_at": NOW, "last_active_at": NOW})),
    )
    conn.commit()


def test_register_instance_inserts_new(fresh_conn):
    _register_instance(fresh_conn, "generic_abc12345")
    row = fresh_conn.execute(
        "SELECT service_id, kind, framework, node, \"user\" FROM instance "
        "WHERE service_id=?", ("generic_abc12345",)
    ).fetchone()
    assert row["kind"] == "三方"
    assert row["framework"] == "langchain"
    assert row["user"] == "alice"


def test_register_instance_upsert_updates_node_address(fresh_conn):
    """同 service_id 二次注册 → 行数 1，node/address 被更新。"""
    _register_instance(fresh_conn, "generic_abc12345", node="192.168.0.11",
                       address="http://old")
    _register_instance(fresh_conn, "generic_abc12345", node="192.168.0.99",
                       address="http://new")

    cnt = fresh_conn.execute(
        "SELECT count(*) FROM instance WHERE service_id=?", ("generic_abc12345",)
    ).fetchone()[0]
    assert cnt == 1

    row = fresh_conn.execute(
        'SELECT node, data FROM instance WHERE service_id=?', ("generic_abc12345",)
    ).fetchone()
    assert row["node"] == "192.168.0.99"
    assert json.loads(row["data"])["address"] == "http://new"


# ── update_instance：部分更新 + 404 ──────────────────────────

def test_update_instance_node_address(fresh_conn):
    """PATCH /api/instances/{sid}：只改 node/address，其他不动。"""
    _register_instance(fresh_conn, "generic_u1", node="10.0.0.1",
                       address="http://old")
    # 先取旧 data
    old = fresh_conn.execute(
        'SELECT data FROM instance WHERE service_id=?', ("generic_u1",)
    ).fetchone()
    old_data = json.loads(old["data"])

    # UPDATE node + data.address
    new_data = dict(old_data, address="http://new")
    fresh_conn.execute(
        'UPDATE instance SET node=?, data=? WHERE registry=? AND service_id=?',
        ("10.0.0.2", json.dumps(new_data), INS_REG, "generic_u1"),
    )
    fresh_conn.commit()

    row = fresh_conn.execute(
        'SELECT node, data FROM instance WHERE service_id=?', ("generic_u1",)
    ).fetchone()
    assert row["node"] == "10.0.0.2"
    assert json.loads(row["data"])["address"] == "http://new"
    # created_at 保持
    assert json.loads(row["data"])["created_at"] == old_data["created_at"]


def test_update_instance_missing_returns_zero_rowcount(fresh_conn):
    """更新不存在的 sid → rowcount=0 → Python 层 404。"""
    cur = fresh_conn.execute(
        'UPDATE instance SET node=? WHERE registry=? AND service_id=?',
        ("x", INS_REG, "no_such"),
    )
    assert cur.rowcount == 0


# ── deregister_instance：幂等删 ──────────────────────────────

def test_deregister_instance_existing(fresh_conn):
    _register_instance(fresh_conn, "generic_d1")
    cur = fresh_conn.execute(
        'DELETE FROM instance WHERE registry=? AND service_id=?',
        (INS_REG, "generic_d1"),
    )
    fresh_conn.commit()
    assert cur.rowcount == 1


def test_deregister_instance_idempotent_on_missing(fresh_conn):
    cur = fresh_conn.execute(
        'DELETE FROM instance WHERE registry=? AND service_id=?',
        (INS_REG, "never"),
    )
    assert cur.rowcount == 0                          # → None（幂等）


# ── list_instances：等值过滤 ─────────────────────────────────

def test_list_all_instances(appliance_conn):
    rows = appliance_conn.execute(
        "SELECT service_id FROM instance WHERE registry=?", (INS_REG,)
    ).fetchall()
    assert len(rows) == 3


def test_list_filter_by_node(appliance_conn):
    """?node=192.168.0.11 → 走 idx_instance_node。"""
    rows = appliance_conn.execute(
        "SELECT service_id, framework FROM instance "
        "WHERE registry=? AND node=?", (INS_REG, "192.168.0.11")
    ).fetchall()
    assert len(rows) == 2
    assert {r["framework"] for r in rows} == {"langchain", "llama_index"}


def test_list_filter_by_user(appliance_conn):
    """?user=alice → 走 idx_instance_user。"""
    rows = appliance_conn.execute(
        'SELECT service_id FROM instance WHERE registry=? AND "user"=?',
        (INS_REG, "alice"),
    ).fetchall()
    assert len(rows) == 2


def test_list_filter_by_framework_and_version(appliance_conn):
    """?framework=langchain&framework_version=0.2.0 → 走 idx_instance_fw。"""
    rows = appliance_conn.execute(
        "SELECT service_id FROM instance "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, "langchain", "0.2.0"),
    ).fetchall()
    assert len(rows) == 1
    assert rows[0]["service_id"] == "generic_a1b2c3d4"


def test_list_filter_by_kind(appliance_conn):
    """?kind=九问 —— kind 非 hot 列，全表扫描（可接受，区分度低）。"""
    rows = appliance_conn.execute(
        "SELECT service_id FROM instance WHERE registry=? AND kind=?",
        (INS_REG, "九问"),
    ).fetchall()
    assert len(rows) == 1
    assert rows[0]["service_id"] == "generic_11223344"


# ── status 落库（data JSON，gateway 写入）─────────────────────

def test_status_persisted_in_data_json_not_column(appliance_conn):
    """instance 表无 status 列——status 落在行内 data JSON（data.status）。

    注册中心不收心跳、不派生状态：gateway 据元戎
    List 经 PATCH 写入，默认 运行。
    """
    cols = {r["name"] for r in appliance_conn.execute("PRAGMA table_info(instance)")}
    assert "status" not in cols

    # 种子数据里的实例均无 data.status（视为 运行），验证 COALESCE 语义
    rows = appliance_conn.execute(
        "SELECT service_id, COALESCE(json_extract(data, '$.status'), '运行') AS status "
        "FROM instance WHERE registry=?",
        (INS_REG,),
    ).fetchall()
    assert rows
    assert all(r["status"] == "运行" for r in rows)
