"""schema 校验：rqlite 后端的表 / 列 / 索引必须与实现文档一致。

对照 ``test_schema.py``，同一组结构断言改用 ``Backend.query`` 跑在 rqlite
Docker 集群上。与 sqlite 原生驱动的调用差异：
- ``PRAGMA table_info(table)`` 在 rqlite 上通过 ``/db/query`` 执行，
  返回 ``list[dict]``（含 cid/name/type/notnull/dflt_value/pk）。
- ``sqlite_master`` 查询语法一致，但 rqlite 返回值为 ``list[dict]``。
- 不存在 ``empty.db`` / ``appliance.db`` 预制文件概念：空 schema 用
  ``rqlite_backend``（仅建表无数据），样例数据用 ``rqlite_seeded_backend``。

端口号见 ``conftest.RQLITE_DOCKER_ENDPOINTS``（常量）。
"""

from __future__ import annotations

from a2x_registry.common.db import init_schema


# ── 表存在性 ──────────────────────────────────────────────────

def test_four_tables_exist(rqlite_backend):
    """rqlite 上必须含 4 张表：registry_meta / service / image / instance。

    rqlite 集群是 session 级共享的；fixture 在 teardown 会 DROP 所有表，
    这里查 sqlite_master 只看业务 4 表（rqlite 自身不预置额外表）。
    """
    rows = rqlite_backend.query(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
    )
    names = {r["name"] for r in rows}
    assert {"registry_meta", "service", "image", "instance"} <= names


def test_init_schema_creates_only_four_tables(rqlite_backend):
    """rqlite 上 init_schema 后业务表恰为 4 张（无多余表）。"""
    rows = rqlite_backend.query(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
    )
    names = {r["name"] for r in rows}
    assert names == {"registry_meta", "service", "image", "instance"}


# ── 列定义 ────────────────────────────────────────────────────

def _columns(backend, table):
    rows = backend.query(f"PRAGMA table_info({table})")
    return {r["name"]: r for r in rows}


def test_registry_meta_columns(rqlite_backend):
    """registry_meta(registry PK, kind NOT NULL, config)。"""
    cols = _columns(rqlite_backend, "registry_meta")
    assert set(cols) == {"registry", "kind", "config"}
    assert cols["registry"]["pk"] == 1           # PRIMARY KEY
    assert cols["kind"]["notnull"] == 1          # kind 显式 NOT NULL
    assert cols["config"]["notnull"] == 0         # 可空


def test_service_columns(rqlite_backend):
    """service 主键 (registry, service_id)，热列 name/description，data JSON NOT NULL。"""
    cols = _columns(rqlite_backend, "service")
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


def test_image_columns(rqlite_backend):
    """image 主键 (registry, service_id)，framework/version/version_key/is_default 热，uploaded_by 热，is_default 默认 0。"""
    cols = _columns(rqlite_backend, "image")
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


def test_instance_columns(rqlite_backend):
    """instance 主键 (registry, service_id)，user 是保留字需引号，framework/node 可空。"""
    cols = _columns(rqlite_backend, "instance")
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

def test_indexes_exist(rqlite_backend):
    """6 个业务索引必须存在。"""
    rows = rqlite_backend.query(
        "SELECT name FROM sqlite_master WHERE type='index' "
        "AND name NOT LIKE 'sqlite_autoindex%'"
    )
    names = {r["name"] for r in rows}
    expected = {
        "idx_service_type",
        "idx_image_fw", "idx_image_fw_ver",
        "idx_instance_node", "idx_instance_fw", "idx_instance_user",
    }
    assert expected <= names, f"缺失索引：{expected - names}"


def test_image_index_covers_framework_and_version(rqlite_backend):
    """idx_image_fw_ver 覆盖 (registry, framework, framework_version) —— launch-spec 查询走它。"""
    rows = rqlite_backend.query(
        "SELECT sql FROM sqlite_master WHERE name='idx_image_fw_ver'"
    )
    assert rows, "idx_image_fw_ver 不存在"
    sql = rows[0]["sql"].lower()
    assert "framework" in sql
    assert "framework_version" in sql


def test_instance_index_covers_node(rqlite_backend):
    """idx_instance_node 覆盖 (registry, node) —— expire_node 批量删走它。"""
    rows = rqlite_backend.query(
        "SELECT sql FROM sqlite_master WHERE name='idx_instance_node'"
    )
    assert rows, "idx_instance_node 不存在"
    assert "node" in rows[0]["sql"].lower()


# ── CREATE TABLE IF NOT EXISTS 幂等性 ─────────────────────────

def test_init_schema_is_idempotent(rqlite_backend):
    """重复调 init_schema 不报错、不新增表（CREATE TABLE IF NOT EXISTS 语义）。

    rqlite 版：init_schema 内部对 RqliteConnection 拆分 SCHEMA_SQL 为单语句
    逐条 execute；二次调用同样不会因表已存在而报错。
    """
    init_schema(rqlite_backend.conn)              # 第二次执行
    rows = rqlite_backend.query(
        "SELECT name FROM sqlite_master WHERE type='table'"
    )
    tables = {r["name"] for r in rows}
    assert {"registry_meta", "service", "image", "instance"} <= tables


# ── rqlite 特有：seeded backend 上校验样例数据完整性 ──────────

def test_seeded_backend_has_expected_row_counts(rqlite_seeded_backend):
    """seeded backend 导入 seed_appliance.sql 后，各表行数与 sqlite 版 appliance.db 一致。

    sqlite 版用预制 ``appliance.db`` 文件做只读校验；rqlite 改为运行时
    导入 seed_appliance.sql（``_seed_appliance_data`` fixture 内已做语句拆分）。
    """
    counts = {
        "registry_meta": rqlite_seeded_backend.query(
            "SELECT count(*) AS c FROM registry_meta"
        )[0]["c"],
        "service": rqlite_seeded_backend.query(
            "SELECT count(*) AS c FROM service"
        )[0]["c"],
        "image": rqlite_seeded_backend.query(
            "SELECT count(*) AS c FROM image"
        )[0]["c"],
        "instance": rqlite_seeded_backend.query(
            "SELECT count(*) AS c FROM instance"
        )[0]["c"],
    }
    assert counts == {"registry_meta": 3, "service": 1, "image": 3, "instance": 3}
