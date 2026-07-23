"""register/ 通用 CRUD 的 rqlite 后端验证。

对照 ``test_crud_sql.py``，同一组业务断言改用 ``Backend`` 抽象跑在 rqlite
Docker 集群上。与 sqlite 原生驱动的调用差异：
- ``backend.execute(sql, args)`` 不返回 cursor（无 ``rowcount``），写操作
  的"影响行数"通过事后 ``SELECT count(*)`` 间接校验。
- ``backend.query(sql, args)`` 直接返回 ``list[dict]``，``fetchone`` 对应
  ``rows[0] if rows else None``，``fetchall`` 对应返回值本身。
- 无 ``conn.commit()`` —— rqlite 每次 execute 即一次 Raft 提交。
- UNIQUE 冲突等错误以 ``RuntimeError`` 抛出（rqlite HTTP API 错误），
  而非 sqlite3 的 ``IntegrityError``。

端口号见 ``conftest.RQLITE_DOCKER_ENDPOINTS``（常量）。
"""

from __future__ import annotations

import json
import pytest

REGISTRY = "default"
NOW = "2026-07-13T10:00:00Z"


# ── create_registry：幂等登记 ────────────────────────────────

def test_create_registry_idempotent(rqlite_backend):
    """同 (registry, kind) 重复登记 → registry_meta 只 1 行。

    SQL 模式：INSERT OR IGNORE（rqlite 同样支持）。
    """
    sql = "INSERT OR IGNORE INTO registry_meta(registry, kind, config) VALUES (?, ?, ?)"
    rqlite_backend.execute(sql, (REGISTRY, "service", None))
    rqlite_backend.execute(sql, (REGISTRY, "service", None))   # 重复

    rows = rqlite_backend.query(
        "SELECT count(*) AS c FROM registry_meta WHERE registry=?", (REGISTRY,)
    )
    assert rows[0]["c"] == 1


def test_create_registry_routes_by_kind(rqlite_backend):
    """不同 kind 的注册表各行其道：service / image / instance。"""
    sql = "INSERT OR IGNORE INTO registry_meta(registry, kind) VALUES (?, ?)"
    for name, kind in [("default", "service"),
                       ("images", "image"),
                       ("instances", "instance")]:
        rqlite_backend.execute(sql, (name, kind))

    rows = rqlite_backend.query(
        "SELECT registry, kind FROM registry_meta ORDER BY registry"
    )
    kinds = {r["registry"]: r["kind"] for r in rows}
    assert kinds == {"default": "service", "images": "image", "instances": "instance"}


# ── register：幂等 upsert ────────────────────────────────────

def _register_service(backend, sid, name="svc", desc="d", data=None):
    """模拟 register/service.py 的 register() —— ON CONFLICT upsert。

    rqlite 同样支持 ``ON CONFLICT(...) DO UPDATE SET``（SQLite UPSERT 语法）。
    """
    data = data or {"endpoint": "http://x"}
    backend.execute(
        """
        INSERT INTO service(registry, service_id, type, source, name, description,
                            data, created_at, updated_at)
        VALUES (?, ?, 'generic', 'api_config', ?, ?, ?, ?, ?)
        ON CONFLICT(registry, service_id) DO UPDATE SET
            name=excluded.name,
            description=excluded.description,
            data=excluded.data,
            updated_at=excluded.updated_at
        """,
        (REGISTRY, sid, name, desc, json.dumps(data), NOW, NOW),
    )


def test_register_inserts_new_row(rqlite_backend):
    _register_service(rqlite_backend, "svc_1", name="hello")
    rows = rqlite_backend.query(
        "SELECT service_id, name, data FROM service WHERE service_id=?",
        ("svc_1",),
    )
    assert len(rows) == 1
    row = rows[0]
    assert row["name"] == "hello"
    assert json.loads(row["data"])["endpoint"] == "http://x"


def test_register_upsert_updates_existing(rqlite_backend):
    """同 service_id 二次 register → 行数仍 1，字段被更新。"""
    _register_service(rqlite_backend, "svc_1", name="v1")
    _register_service(rqlite_backend, "svc_1", name="v2", data={"endpoint": "http://y"})

    rows = rqlite_backend.query(
        "SELECT count(*) AS c, name, data FROM service WHERE service_id=?",
        ("svc_1",),
    )
    assert rows[0]["c"] == 1

    rows = rqlite_backend.query(
        "SELECT name, data FROM service WHERE service_id=?", ("svc_1",)
    )
    assert rows[0]["name"] == "v2"
    assert json.loads(rows[0]["data"])["endpoint"] == "http://y"


def test_register_data_json_roundtrip(rqlite_backend):
    """data 列存复杂 JSON，写入读出等价（rqlite TEXT + JSON1）。"""
    payload = {"nested": {"a": [1, 2, 3]}, "unicode": "中文测试"}
    _register_service(rqlite_backend, "svc_j", data=payload)
    rows = rqlite_backend.query(
        "SELECT data FROM service WHERE service_id=?", ("svc_j",)
    )
    assert json.loads(rows[0]["data"]) == payload


# ── patch：部分更新 ──────────────────────────────────────────

def test_patch_updates_provided_fields_only(rqlite_backend):
    """patch 只更新传入字段，其他列保持不变。"""
    _register_service(rqlite_backend, "svc_p", name="orig", desc="orig-desc")

    rqlite_backend.execute(
        "UPDATE service SET name=?, updated_at=? WHERE registry=? AND service_id=?",
        ("patched", NOW, REGISTRY, "svc_p"),
    )

    rows = rqlite_backend.query(
        "SELECT name, description, data FROM service WHERE service_id=?",
        ("svc_p",),
    )
    assert rows[0]["name"] == "patched"
    assert rows[0]["description"] == "orig-desc"        # 未传，保持


def test_patch_missing_leaves_table_unchanged(rqlite_backend):
    """patch 不存在的 service_id → 0 行受影响（rqlite 无 rowcount，改用 count 校验）。

    sqlite 版用 ``cur.rowcount == 0``；rqlite ``Backend.execute`` 不返回
    rowcount，这里改为：操作前后 service 表行数均为 0（无任何行被改）。
    """
    before = rqlite_backend.query("SELECT count(*) AS c FROM service")[0]["c"]
    rqlite_backend.execute(
        "UPDATE service SET name=? WHERE registry=? AND service_id=?",
        ("x", REGISTRY, "no_such_sid"),
    )
    after = rqlite_backend.query("SELECT count(*) AS c FROM service")[0]["c"]
    assert before == after == 0


# ── deregister：幂等删 ───────────────────────────────────────

def test_deregister_removes_existing_row(rqlite_backend):
    """删存在的行 → 行消失（sqlite 版用 ``cur.rowcount == 1``）。

    rqlite 无 rowcount：改为删后 SELECT count 校验 0 → 1 → 0。
    """
    _register_service(rqlite_backend, "svc_d")
    assert rqlite_backend.query(
        "SELECT count(*) AS c FROM service WHERE service_id=?", ("svc_d",)
    )[0]["c"] == 1

    rqlite_backend.execute(
        "DELETE FROM service WHERE registry=? AND service_id=?",
        (REGISTRY, "svc_d"),
    )
    assert rqlite_backend.query(
        "SELECT count(*) AS c FROM service WHERE service_id=?", ("svc_d",)
    )[0]["c"] == 0


def test_deregister_idempotent_on_missing(rqlite_backend):
    """二次 deregister → 表行数不变（幂等）。

    sqlite 版用 ``cur.rowcount == 0``；rqlite 改为前后 count 一致。
    """
    before = rqlite_backend.query("SELECT count(*) AS c FROM service")[0]["c"]
    rqlite_backend.execute(
        "DELETE FROM service WHERE registry=? AND service_id=?",
        (REGISTRY, "never_existed"),
    )
    after = rqlite_backend.query("SELECT count(*) AS c FROM service")[0]["c"]
    assert before == after                          # → None（幂等）


# ── query：等值过滤 ──────────────────────────────────────────

def test_query_all_when_no_filter(rqlite_backend):
    for i in range(3):
        _register_service(rqlite_backend, f"svc_{i}")
    rows = rqlite_backend.query(
        "SELECT service_id FROM service WHERE registry=? ORDER BY service_id",
        (REGISTRY,),
    )
    assert len(rows) == 3


def test_query_equality_filter_on_hot_column(rqlite_backend):
    """热列 type 走索引 idx_service_type 等值过滤。"""
    rqlite_backend.execute(
        "INSERT INTO service(registry, service_id, type, source, data, created_at, updated_at) "
        "VALUES (?, 'a', 'generic', 'api_config', '{}', ?, ?)",
        (REGISTRY, NOW, NOW),
    )
    rqlite_backend.execute(
        "INSERT INTO service(registry, service_id, type, source, data, created_at, updated_at) "
        "VALUES (?, 'b', 'a2a', 'api_config', '{}', ?, ?)",
        (REGISTRY, NOW, NOW),
    )

    rows = rqlite_backend.query(
        "SELECT service_id FROM service WHERE registry=? AND type=?",
        (REGISTRY, "a2a"),
    )
    assert [r["service_id"] for r in rows] == ["b"]


# ── data JSON 提取（rqlite 支持 JSON1） ──────────────────────

def test_query_filter_by_json_field(rqlite_backend):
    """data 是 JSON，可用 json_extract 做条件过滤（rqlite 内嵌 SQLite 支持 JSON1）。"""
    _register_service(rqlite_backend, "s1", data={"endpoint": "http://a"})
    _register_service(rqlite_backend, "s2", data={"endpoint": "http://b"})

    rows = rqlite_backend.query(
        "SELECT service_id FROM service "
        "WHERE registry=? AND json_extract(data, '$.endpoint')=?",
        (REGISTRY, "http://a"),
    )
    assert [r["service_id"] for r in rows] == ["s1"]


# ── rqlite 特有：错误以 RuntimeError 暴露 ────────────────────

def test_unique_violation_raises_runtime_error(rqlite_backend):
    """rqlite UNIQUE 冲突 → RuntimeError（非 sqlite3.IntegrityError）。

    sqlite 版的 ``test_backend_execute_rollback_on_exception`` 用
    ``pytest.raises(Exception)``；这里显式断言 RuntimeError，锁死 rqlite
    HTTP 错误的异常类型，防止未来包装层变更后悄悄漂移。
    """
    rqlite_backend.execute(
        "CREATE TABLE t(x INTEGER UNIQUE)"
    )
    rqlite_backend.execute("INSERT INTO t(x) VALUES (?)", (1,))

    with pytest.raises(RuntimeError):
        rqlite_backend.execute("INSERT INTO t(x) VALUES (?)", (1,))   # UNIQUE 冲突

    rows = rqlite_backend.query("SELECT count(*) AS c FROM t")
    assert rows[0]["c"] == 1                          # 回滚，仍只有 1 行
