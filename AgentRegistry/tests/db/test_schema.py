"""schema 校验：预制 .db 文件的表 / 列 / 索引必须与实现一致。

锁定表结构的真源，防止 `register/store.py` 实现时漂移。
"""

from __future__ import annotations

import sqlite3


# ── 表存在性 ──────────────────────────────────────────────────

def test_four_tables_exist(appliance_conn):
    """appliance.db 必须含 4 张表：registry_meta / service / image / instance。"""
    rows = appliance_conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
    ).fetchall()
    names = {r["name"] for r in rows}
    assert names == {"registry_meta", "service", "image", "instance"}


def test_empty_db_has_same_tables(empty_db_path):
    """empty.db 也必须有同样 4 张表（schema 与 appliance 一致）。"""
    conn = sqlite3.connect(str(empty_db_path))
    try:
        rows = conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
        ).fetchall()
        names = {r[0] for r in rows}
        assert names == {"registry_meta", "service", "image", "instance"}
    finally:
        conn.close()


# ── 列定义 ────────────────────────────────────────────────────

def _columns(conn, table):
    return {r["name"]: r for r in conn.execute(f"PRAGMA table_info({table})")}


def test_registry_meta_columns(appliance_conn):
    """registry_meta(registry PK, kind NOT NULL, config)。

    注意：SQLite 中 ``TEXT PRIMARY KEY`` 的 ``notnull`` 标志为 0
    （只有 ``INTEGER PRIMARY KEY`` 隐式 NOT NULL）；PK 约束本身保证唯一性，
    NULL 由应用层保证。实现时若需强约束可写 ``registry TEXT PRIMARY KEY NOT NULL``。
    """
    cols = _columns(appliance_conn, "registry_meta")
    assert set(cols) == {"registry", "kind", "config"}
    assert cols["registry"]["pk"] == 1           # PRIMARY KEY
    assert cols["kind"]["notnull"] == 1          # kind 显式 NOT NULL
    assert cols["config"]["notnull"] == 0         # 可空


def test_service_columns(appliance_conn):
    """service 主键 (registry, service_id)，热列 name/description，data JSON NOT NULL。"""
    cols = _columns(appliance_conn, "service")
    assert set(cols) == {
        "registry", "service_id", "type", "source",
        "name", "description", "data",
        "created_at", "updated_at",
    }
    # 复合主键
    assert cols["registry"]["pk"] == 1
    assert cols["service_id"]["pk"] == 2
    # NOT NULL 约束
    for nn in ("registry", "service_id", "type", "source", "data",
               "created_at", "updated_at"):
        assert cols[nn]["notnull"] == 1, f"{nn} 应 NOT NULL"
    # 热列可空
    assert cols["name"]["notnull"] == 0
    assert cols["description"]["notnull"] == 0


def test_image_columns(appliance_conn):
    """image 主键 (registry, service_id)，framework/version/version_key/is_default 热，uploaded_by 热，is_default 默认 0。"""
    cols = _columns(appliance_conn, "image")
    assert set(cols) == {
        "registry", "service_id", "framework", "framework_version",
        "version_key", "is_default", "uploaded_by", "data",
    }
    assert cols["registry"]["pk"] == 1
    assert cols["service_id"]["pk"] == 2
    for nn in ("registry", "service_id", "framework",
               "framework_version", "version_key", "is_default", "data"):
        assert cols[nn]["notnull"] == 1, f"{nn} 应 NOT NULL"
    # is_default 默认值 0
    assert cols["is_default"]["dflt_value"] == "0"


def test_instance_columns(appliance_conn):
    """instance 主键 (registry, service_id)，user 是保留字需引号，framework/node 可空。"""
    cols = _columns(appliance_conn, "instance")
    assert set(cols) == {
        "registry", "service_id", "kind", "framework",
        "framework_version", "node", "user", "data",
    }
    assert cols["registry"]["pk"] == 1
    assert cols["service_id"]["pk"] == 2
    # kind / data NOT NULL（每实例必有类型 + 元数据）
    assert cols["kind"]["notnull"] == 1
    assert cols["data"]["notnull"] == 1
    # framework / node / user 可空（九问类可能无 framework；注销边界）
    for nullable in ("framework", "framework_version", "node", "user"):
        assert cols[nullable]["notnull"] == 0, f"{nullable} 应可空"


# ── 索引 ──────────────────────────────────────────────────────

def test_indexes_exist(appliance_conn):
    """6 个业务索引必须存在。"""
    rows = appliance_conn.execute(
        "SELECT name FROM sqlite_master WHERE type='index' "
        "AND name NOT LIKE 'sqlite_autoindex%'"
    ).fetchall()
    names = {r["name"] for r in rows}
    expected = {
        "idx_service_type",
        "idx_image_fw", "idx_image_fw_ver",
        "idx_instance_node", "idx_instance_fw", "idx_instance_user",
    }
    assert expected <= names, f"缺失索引：{expected - names}"


def test_image_index_covers_framework_and_version(appliance_conn):
    """idx_image_fw_ver 覆盖 (registry, framework, framework_version) —— launch-spec 查询走它。"""
    info = appliance_conn.execute(
        "SELECT sql FROM sqlite_master WHERE name='idx_image_fw_ver'"
    ).fetchone()
    assert info is not None
    sql = info["sql"].lower()
    assert "framework" in sql
    assert "framework_version" in sql


def test_instance_index_covers_node(appliance_conn):
    """idx_instance_node 覆盖 (registry, node) —— expire_node 批量删走它。"""
    info = appliance_conn.execute(
        "SELECT sql FROM sqlite_master WHERE name='idx_instance_node'"
    ).fetchone()
    assert info is not None
    assert "node" in info["sql"].lower()


# ── CREATE TABLE IF NOT EXISTS 幂等性 ─────────────────────────

def test_init_schema_is_idempotent(fresh_conn):
    """重复调 init_schema 不报错、不新增表（CREATE TABLE IF NOT EXISTS 语义）。

    对应要求："启动时一次性建齐（CREATE TABLE IF NOT EXISTS）"。
    复用源码 `a2x_registry.common.db.init_schema` —— 测试与源码共用同一份
    SCHEMA_SQL 真源。
    """
    from a2x_registry.common.db import init_schema

    # 第二次执行——IF NOT EXISTS 必须保证幂等
    init_schema(fresh_conn)
    tables = {r[0] for r in fresh_conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table'"
    )}
    assert tables == {"registry_meta", "service", "image", "instance"}
