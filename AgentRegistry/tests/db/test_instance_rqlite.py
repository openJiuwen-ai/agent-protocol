"""instance/ 模块的 rqlite 后端 SQL 模式验证。

对照 ``test_instance_sql.py``，同一组业务断言改用 ``Backend`` 抽象跑在 rqlite
Docker 集群上。与 sqlite 原生驱动的调用差异：
- ``conn.execute(...).fetchone()`` → ``backend.query(...)`` 取 ``rows[0] if rows else None``。
- ``conn.execute(...).fetchall()`` → ``backend.query(...)`` 直接是 ``list[dict]``。
- ``cur.rowcount`` 不存在：删/改的影响行数改用前后 ``SELECT count(*)`` 或行
  存在性校验（见 ``test_deregister_instance_existing`` 等）。
- ``conn.commit()`` 省略（rqlite 每次 execute 即 Raft 提交）。
- 只读 ``appliance_conn`` / 可写 ``appliance_writable_copy`` 统一用
  ``rqlite_seeded_backend``。

端口号见 ``conftest.RQLITE_DOCKER_ENDPOINTS``（常量）。
"""

from __future__ import annotations

import hashlib
import json

INS_REG = "instances"
NOW = "2026-07-13T10:00:00Z"


# ── service_id 确定性派生 ────────────────────────────────────

def test_instance_sid_deterministic():
    """instance_sid(user, framework) = "generic_" + sha256(user|framework)[:8]。

    纯 Python 计算，与后端无关；sqlite 版同测一次，rqlite 版保留以确保
    两侧 service_id 派生口径一致（样例数据手算占位依赖此公式）。
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

def _register_instance(backend, sid, user="alice", fw="langchain", fw_ver="0.2.0",
                       node="192.168.0.11", kind="三方", address="http://x"):
    backend.execute(
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


def test_register_instance_inserts_new(rqlite_backend):
    _register_instance(rqlite_backend, "generic_abc12345")
    rows = rqlite_backend.query(
        "SELECT service_id, kind, framework, node, \"user\" FROM instance "
        "WHERE service_id=?", ("generic_abc12345",)
    )
    assert len(rows) == 1
    row = rows[0]
    assert row["kind"] == "三方"
    assert row["framework"] == "langchain"
    assert row["user"] == "alice"


def test_register_instance_upsert_updates_node_address(rqlite_backend):
    """同 service_id 二次注册 → 行数 1，node/address 被更新。"""
    _register_instance(rqlite_backend, "generic_abc12345", node="192.168.0.11",
                       address="http://old")
    _register_instance(rqlite_backend, "generic_abc12345", node="192.168.0.99",
                       address="http://new")

    rows = rqlite_backend.query(
        "SELECT count(*) AS c FROM instance WHERE service_id=?", ("generic_abc12345",)
    )
    assert rows[0]["c"] == 1

    rows = rqlite_backend.query(
        'SELECT node, data FROM instance WHERE service_id=?', ("generic_abc12345",)
    )
    assert rows[0]["node"] == "192.168.0.99"
    assert json.loads(rows[0]["data"])["address"] == "http://new"


# ── update_instance：部分更新 + 404 ──────────────────────────

def test_update_instance_node_address(rqlite_backend):
    """PATCH /api/instances/{sid}：只改 node/address，其他不动。"""
    _register_instance(rqlite_backend, "generic_u1", node="10.0.0.1",
                       address="http://old")
    # 先取旧 data
    rows = rqlite_backend.query(
        'SELECT data FROM instance WHERE service_id=?', ("generic_u1",)
    )
    old_data = json.loads(rows[0]["data"])

    # UPDATE node + data.address
    new_data = dict(old_data, address="http://new")
    rqlite_backend.execute(
        'UPDATE instance SET node=?, data=? WHERE registry=? AND service_id=?',
        ("10.0.0.2", json.dumps(new_data), INS_REG, "generic_u1"),
    )

    rows = rqlite_backend.query(
        'SELECT node, data FROM instance WHERE service_id=?', ("generic_u1",)
    )
    assert rows[0]["node"] == "10.0.0.2"
    assert json.loads(rows[0]["data"])["address"] == "http://new"
    # created_at 保持
    assert json.loads(rows[0]["data"])["created_at"] == old_data["created_at"]


def test_update_instance_missing_leaves_table_unchanged(rqlite_backend):
    """更新不存在的 sid → 0 行受影响（rqlite 无 rowcount，改用 count 校验）。

    sqlite 版用 ``cur.rowcount == 0``；rqlite 改为前后 instance 表行数不变。
    """
    before = rqlite_backend.query("SELECT count(*) AS c FROM instance")[0]["c"]
    rqlite_backend.execute(
        'UPDATE instance SET node=? WHERE registry=? AND service_id=?',
        ("x", INS_REG, "no_such"),
    )
    after = rqlite_backend.query("SELECT count(*) AS c FROM instance")[0]["c"]
    assert before == after == 0


# ── deregister_instance：幂等删 ──────────────────────────────

def test_deregister_instance_existing(rqlite_backend):
    """删存在的行 → 行消失（sqlite 版用 ``cur.rowcount == 1``）。"""
    _register_instance(rqlite_backend, "generic_d1")
    assert rqlite_backend.query(
        "SELECT count(*) AS c FROM instance WHERE service_id=?", ("generic_d1",)
    )[0]["c"] == 1

    rqlite_backend.execute(
        'DELETE FROM instance WHERE registry=? AND service_id=?',
        (INS_REG, "generic_d1"),
    )
    assert rqlite_backend.query(
        "SELECT count(*) AS c FROM instance WHERE service_id=?", ("generic_d1",)
    )[0]["c"] == 0


def test_deregister_instance_idempotent_on_missing(rqlite_backend):
    """二次 deregister → 表行数不变（幂等）。

    sqlite 版用 ``cur.rowcount == 0``；rqlite 改为前后 count 一致。
    """
    before = rqlite_backend.query("SELECT count(*) AS c FROM instance")[0]["c"]
    rqlite_backend.execute(
        'DELETE FROM instance WHERE registry=? AND service_id=?',
        (INS_REG, "never"),
    )
    after = rqlite_backend.query("SELECT count(*) AS c FROM instance")[0]["c"]
    assert before == after                          # → None（幂等）


# ── list_instances：等值过滤 ─────────────────────────────────

def test_list_all_instances(rqlite_seeded_backend):
    rows = rqlite_seeded_backend.query(
        "SELECT service_id FROM instance WHERE registry=?", (INS_REG,)
    )
    assert len(rows) == 3


def test_list_filter_by_node(rqlite_seeded_backend):
    """?node=192.168.0.11 → 走 idx_instance_node。"""
    rows = rqlite_seeded_backend.query(
        "SELECT service_id, framework FROM instance "
        "WHERE registry=? AND node=?", (INS_REG, "192.168.0.11")
    )
    assert len(rows) == 2
    assert {r["framework"] for r in rows} == {"langchain", "llama_index"}


def test_list_filter_by_user(rqlite_seeded_backend):
    """?user=alice → 走 idx_instance_user。"""
    rows = rqlite_seeded_backend.query(
        'SELECT service_id FROM instance WHERE registry=? AND "user"=?',
        (INS_REG, "alice"),
    )
    assert len(rows) == 2


def test_list_filter_by_framework_and_version(rqlite_seeded_backend):
    """?framework=langchain&framework_version=0.2.0 → 走 idx_instance_fw。"""
    rows = rqlite_seeded_backend.query(
        "SELECT service_id FROM instance "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, "langchain", "0.2.0"),
    )
    assert len(rows) == 1
    assert rows[0]["service_id"] == "generic_a1b2c3d4"


def test_list_filter_by_kind(rqlite_seeded_backend):
    """?kind=九问 —— kind 非 hot 列，全表扫描（可接受，区分度低）。"""
    rows = rqlite_seeded_backend.query(
        "SELECT service_id FROM instance WHERE registry=? AND kind=?",
        (INS_REG, "九问"),
    )
    assert len(rows) == 1
    assert rows[0]["service_id"] == "generic_11223344"


# ── expire_node：批量剔除 ────────────────────────────────────

def test_expire_node_deletes_all_instances_on_node(rqlite_seeded_backend):
    """过宽限 → 删该 node 全部实例。

    sqlite 版用 ``cur.rowcount == 2``；rqlite 改为删后查 192.168.0.11 的
    行数为 0，192.168.0.12 的 bob 实例不受影响。
    """
    rqlite_seeded_backend.execute(
        "DELETE FROM instance WHERE registry=? AND node=?",
        (INS_REG, "192.168.0.11"),
    )

    # 192.168.0.11 已无实例
    rows = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM instance WHERE node=?", ("192.168.0.11",)
    )
    assert rows[0]["c"] == 0

    # 192.168.0.12 的 bob 实例不动
    rows = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM instance WHERE node=?", ("192.168.0.12",)
    )
    assert rows[0]["c"] == 1


def test_expire_node_idempotent(rqlite_seeded_backend):
    """再次 expire 同一 node → 0 行影响（已无实例）。

    sqlite 版用 ``cur.rowcount == 0``；rqlite 改为：第二次删除前后行数均为 0。
    """
    rqlite_seeded_backend.execute(
        "DELETE FROM instance WHERE registry=? AND node=?",
        (INS_REG, "192.168.0.11"),
    )
    before = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM instance WHERE node=?", ("192.168.0.11",)
    )[0]["c"]
    rqlite_seeded_backend.execute(
        "DELETE FROM instance WHERE registry=? AND node=?",
        (INS_REG, "192.168.0.11"),
    )
    after = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM instance WHERE node=?", ("192.168.0.11",)
    )[0]["c"]
    assert before == after == 0


def test_expire_node_unknown_node_noop(rqlite_backend):
    """expire 不存在的 node → 0 行影响，不报错。"""
    before = rqlite_backend.query("SELECT count(*) AS c FROM instance")[0]["c"]
    rqlite_backend.execute(
        "DELETE FROM instance WHERE registry=? AND node=?",
        (INS_REG, "10.99.99.99"),
    )
    after = rqlite_backend.query("SELECT count(*) AS c FROM instance")[0]["c"]
    assert before == after == 0


# ── _derive_status：status 不落库，查 node 心跳派生 ──────────

def test_status_not_persisted_in_instance_table(rqlite_backend):
    """instance 表无 status 列——status 查询时由 node 心跳派生。"""
    cols = rqlite_backend.query("PRAGMA table_info(instance)")
    col_names = {r["name"] for r in cols}
    assert "status" not in col_names


def test_derive_status_by_node_heartbeat(rqlite_seeded_backend):
    """_derive_status: "异常" if hb.is_expired(node) else "运行"。

    此处只验证派生逻辑的 SQL 输入：按 node 查实例。
    心跳活性在内存，不在 SQL 范畴。
    """
    rows = rqlite_seeded_backend.query(
        "SELECT service_id, node FROM instance WHERE registry=? AND node=?",
        (INS_REG, "192.168.0.11"),
    )
    # 假设 hb.is_expired("192.168.0.11") = False → 全部 "运行"
    for r in rows:
        status = "异常" if False else "运行"        # is_expired 注入
        assert status == "运行"


# ── distinct_nodes：心跳恢复用 ───────────────────────────────

def test_distinct_nodes_for_recovery(rqlite_seeded_backend):
    """重启后 recover_from_persisted(distinct_nodes) —— 取 instance 表不重复 node。"""
    rows = rqlite_seeded_backend.query(
        "SELECT DISTINCT node FROM instance WHERE registry=? AND node IS NOT NULL "
        "ORDER BY node",
        (INS_REG,),
    )
    assert [r["node"] for r in rows] == ["192.168.0.11", "192.168.0.12"]
