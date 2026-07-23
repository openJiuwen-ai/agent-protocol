"""register/store.py SQL 后端契约测试。

对照实现文档：
- init_schema：建 4 表 6 索引（与 common/db.py SCHEMA_SQL 一致）
- registry_meta 路由：name → kind（service/image/instance）
- data JSON 编解码：复杂对象 round-trip
- 启动时一次性建齐（CREATE TABLE IF NOT EXISTS 幂等）
"""

from __future__ import annotations

import sqlite3

import pytest

from a2x_registry.common.db import init_schema


# ── init_schema ───────────────────────────────────────────────

def test_init_schema_creates_four_tables(tmp_path):
    conn = sqlite3.connect(str(tmp_path / "t.db"))
    try:
        init_schema(conn)
        tables = {
            r[0] for r in conn.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            )
        }
        assert tables == {"registry_meta", "service", "image", "instance"}
    finally:
        conn.close()


def test_init_schema_creates_six_indexes(tmp_path):
    conn = sqlite3.connect(str(tmp_path / "t.db"))
    try:
        init_schema(conn)
        indexes = {
            r[0] for r in conn.execute(
                "SELECT name FROM sqlite_master WHERE type='index' "
                "AND name NOT LIKE 'sqlite_autoindex%'"
            )
        }
        # V2 schema: image adds idx_image_by + idx_image_order,
        # instance adds idx_instance_order (2 new indexes).
        assert indexes == {
            "idx_service_type",
            "idx_image_fw", "idx_image_fw_ver",
            "idx_image_by", "idx_image_order",
            "idx_instance_node", "idx_instance_fw", "idx_instance_user",
            "idx_instance_order",
        }
    finally:
        conn.close()


def test_init_schema_is_idempotent(tmp_path):
    conn = sqlite3.connect(str(tmp_path / "t.db"))
    try:
        init_schema(conn)
        init_schema(conn)
        cnt = conn.execute(
            "SELECT count(*) FROM sqlite_master WHERE type='table' "
            "AND name='registry_meta'"
        ).fetchone()[0]
        assert cnt == 1
    finally:
        conn.close()


# ── registry_meta 路由 ────────────────────────────────────────

def test_registry_meta_routes_name_to_kind(fresh_backend):
    """create_registry 往 registry_meta 登记名字→kind，运行时按 name 路由物理表。"""
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    svc.create_registry("default", "service")
    svc.create_registry("images", "image")
    svc.create_registry("instances", "instance")

    rows = fresh_backend.query(
        "SELECT registry, kind FROM registry_meta ORDER BY registry"
    )
    kinds = {r["registry"]: r["kind"] for r in rows}
    assert kinds == {
        "default": "service",
        "images": "image",
        "instances": "instance",
    }


def test_registry_meta_create_then_query_kind(fresh_backend):
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    svc.create_registry("images", "image")
    assert svc.get_kind("images") == "image"


def test_registry_meta_unknown_name_returns_none(fresh_backend):
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    assert svc.get_kind("no_such_reg") is None


# ── data JSON 编解码 round-trip ───────────────────────────────

def test_data_json_roundtrip_complex(fresh_backend):
    """data 列存复杂 JSON，写入读出等价（嵌套 / unicode / 数组）。"""
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    svc.create_registry("default", "service")
    payload = {
        "nested": {"a": [1, 2, 3], "b": {"c": True}},
        "unicode": "中文测试",
        "none": None,
        "float": 3.14,
    }
    svc.register(
        "default",
        {
            "service_id": "s1",
            "type": "generic",
            "source": "api_config",
            "name": "n",
            "description": "d",
            "data": payload,
        },
    )
    rows = svc.query("default")
    assert rows[0]["data"] == payload


def test_data_json_empty_dict(fresh_backend):
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    svc.create_registry("default", "service")
    svc.register(
        "default",
        {
            "service_id": "s1",
            "type": "generic",
            "source": "api_config",
            "name": "n",
            "description": "d",
            "data": {},
        },
    )
    rows = svc.query("default")
    assert rows[0]["data"] == {}


# ── 启动模式建表（A2X_REGISTRY_MODE） ─────────────────────────

def test_generic_mode_only_service_registry_needed(fresh_backend):
    """通用模式：只需 service 物理表存在即可（image/instance 表虽建但不登记）。"""
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    svc.create_registry("default", "service")  # 通用模式仅此
    assert svc.list_registries() == {"default": "service"}
    # image / instance 物理表已存在但无注册表登记
    assert svc.get_kind("images") is None
    assert svc.get_kind("instances") is None


def test_appliance_mode_creates_three_registries(fresh_backend):
    """appliance 模式：启动时另由镜像/实例管理 create_registry 镜像/实例表。"""
    from a2x_registry.register.service import RegistryTableService

    svc = RegistryTableService(fresh_backend)
    svc.create_registry("default", "service")
    svc.create_registry("images", "image")
    svc.create_registry("instances", "instance")
    assert len(svc.list_registries()) == 3
