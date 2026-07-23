"""register/ 通用 CRUD 的原生 SQL 模式验证。

对照`register/service.py` 的函数契约：
- create_registry(name, kind)  —— 幂等登记
- register(name, entry)        —— 幂等 upsert（按 service_id）
- patch(name, service_id, fields) —— 部分更新（不存在 → 404）
- deregister(name, service_id) —— 幂等删（已删 → False）
- query(name, filter=None)     —— 等值过滤读

这些测试直接跑 SQL，锁死未来 Python 包装要用的 SQL 模式。
"""

from __future__ import annotations

import json
import sqlite3
import time

REGISTRY = "default"
NOW = "2026-07-13T10:00:00Z"


# ── create_registry：幂等登记 ────────────────────────────────

def test_create_registry_idempotent(fresh_conn):
    """同 (registry, kind) 重复登记 → registry_meta 只 1 行。

    SQL 模式：INSERT OR IGNORE（rqlite 同样支持）。
    """
    sql = "INSERT OR IGNORE INTO registry_meta(registry, kind, config) VALUES (?, ?, ?)"
    fresh_conn.execute(sql, (REGISTRY, "service", None))
    fresh_conn.execute(sql, (REGISTRY, "service", None))   # 重复
    fresh_conn.commit()

    cnt = fresh_conn.execute(
        "SELECT count(*) FROM registry_meta WHERE registry=?", (REGISTRY,)
    ).fetchone()[0]
    assert cnt == 1


def test_create_registry_routes_by_kind(fresh_conn):
    """不同 kind 的注册表各行其道：service / image / instance。"""
    sql = "INSERT OR IGNORE INTO registry_meta(registry, kind) VALUES (?, ?)"
    for name, kind in [("default", "service"),
                        ("images", "image"),
                        ("instances", "instance")]:
        fresh_conn.execute(sql, (name, kind))
    fresh_conn.commit()

    rows = fresh_conn.execute(
        "SELECT registry, kind FROM registry_meta ORDER BY registry"
    ).fetchall()
    kinds = {r["registry"]: r["kind"] for r in rows}
    assert kinds == {"default": "service", "images": "image", "instances": "instance"}


# ── register：幂等 upsert ────────────────────────────────────

def _register_service(conn, sid, name="svc", desc="d", data=None):
    """模拟 register/service.py 的 register() —— ON CONFLICT upsert。"""
    data = data or {"endpoint": "http://x"}
    conn.execute(
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
    conn.commit()


def test_register_inserts_new_row(fresh_conn):
    _register_service(fresh_conn, "svc_1", name="hello")
    row = fresh_conn.execute(
        "SELECT service_id, name, data FROM service WHERE service_id=?",
        ("svc_1",),
    ).fetchone()
    assert row is not None
    assert row["name"] == "hello"
    assert json.loads(row["data"])["endpoint"] == "http://x"


def test_register_upsert_updates_existing(fresh_conn):
    """同 service_id 二次 register → 行数仍 1，字段被更新（updated_at 推进）。"""
    _register_service(fresh_conn, "svc_1", name="v1")
    _register_service(fresh_conn, "svc_1", name="v2", data={"endpoint": "http://y"})

    cnt = fresh_conn.execute(
        "SELECT count(*) FROM service WHERE service_id=?", ("svc_1",)
    ).fetchone()[0]
    assert cnt == 1

    row = fresh_conn.execute(
        "SELECT name, data FROM service WHERE service_id=?", ("svc_1",)
    ).fetchone()
    assert row["name"] == "v2"
    assert json.loads(row["data"])["endpoint"] == "http://y"


def test_register_data_json_roundtrip(fresh_conn):
    """data 列存复杂 JSON，写入读出等价（SQLite TEXT + JSON1）。"""
    payload = {"nested": {"a": [1, 2, 3]}, "unicode": "中文测试"}
    _register_service(fresh_conn, "svc_j", data=payload)
    row = fresh_conn.execute(
        "SELECT data FROM service WHERE service_id=?", ("svc_j",)
    ).fetchone()
    assert json.loads(row["data"]) == payload


# ── patch：部分更新 ──────────────────────────────────────────

def test_patch_updates_provided_fields_only(fresh_conn):
    """patch 只更新传入字段，其他列保持不变。"""
    _register_service(fresh_conn, "svc_p", name="orig", desc="orig-desc")

    fresh_conn.execute(
        "UPDATE service SET name=?, updated_at=? WHERE registry=? AND service_id=?",
        ("patched", NOW, REGISTRY, "svc_p"),
    )
    fresh_conn.commit()

    row = fresh_conn.execute(
        "SELECT name, description, data FROM service WHERE service_id=?",
        ("svc_p",),
    ).fetchone()
    assert row["name"] == "patched"
    assert row["description"] == "orig-desc"        # 未传，保持


def test_patch_missing_returns_zero_rowcount(fresh_conn):
    """patch 不存在的 service_id → rowcount=0 → Python 层映射 404。"""
    cur = fresh_conn.execute(
        "UPDATE service SET name=? WHERE registry=? AND service_id=?",
        ("x", REGISTRY, "no_such_sid"),
    )
    assert cur.rowcount == 0


# ── deregister：幂等删 ───────────────────────────────────────

def test_deregister_returns_true_on_existing(fresh_conn):
    _register_service(fresh_conn, "svc_d")
    cur = fresh_conn.execute(
        "DELETE FROM service WHERE registry=? AND service_id=?",
        (REGISTRY, "svc_d"),
    )
    fresh_conn.commit()
    assert cur.rowcount == 1                          # Python 层 → True


def test_deregister_idempotent_on_missing(fresh_conn):
    """二次 deregister → rowcount=0 → Python 层映射 False（幂等）。"""
    cur = fresh_conn.execute(
        "DELETE FROM service WHERE registry=? AND service_id=?",
        (REGISTRY, "never_existed"),
    )
    fresh_conn.commit()
    assert cur.rowcount == 0                          # → False


# ── query：等值过滤 ──────────────────────────────────────────

def test_query_all_when_no_filter(fresh_conn):
    for i in range(3):
        _register_service(fresh_conn, f"svc_{i}")
    rows = fresh_conn.execute(
        "SELECT service_id FROM service WHERE registry=? ORDER BY service_id",
        (REGISTRY,),
    ).fetchall()
    assert len(rows) == 3


def test_query_equality_filter_on_hot_column(fresh_conn):
    """热列 type 走索引 idx_service_type 等值过滤。"""
    fresh_conn.execute(
        "INSERT INTO service(registry, service_id, type, source, data, created_at, updated_at) "
        "VALUES (?, 'a', 'generic', 'api_config', '{}', ?, ?), "
        "       (?, 'b', 'a2a',       'api_config', '{}', ?, ?)",
        (REGISTRY, NOW, NOW, REGISTRY, NOW, NOW),
    )
    fresh_conn.commit()

    rows = fresh_conn.execute(
        "SELECT service_id FROM service WHERE registry=? AND type=?",
        (REGISTRY, "a2a"),
    ).fetchall()
    assert [r["service_id"] for r in rows] == ["b"]


# ── data JSON 提取（SQLite JSON1） ───────────────────────────

def test_query_filter_by_json_field(fresh_conn):
    """data 是 JSON，可用 json_extract 做条件过滤（rqlite 同支持）。"""
    _register_service(fresh_conn, "s1", data={"endpoint": "http://a"})
    _register_service(fresh_conn, "s2", data={"endpoint": "http://b"})

    rows = fresh_conn.execute(
        "SELECT service_id FROM service "
        "WHERE registry=? AND json_extract(data, '$.endpoint')=?",
        (REGISTRY, "http://a"),
    ).fetchall()
    assert [r["service_id"] for r in rows] == ["s1"]
