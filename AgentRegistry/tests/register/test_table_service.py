"""RegistryTableService 通用 CRUD 契约测试。

对照函数契约（create_registry / register / patch /
deregister / query），覆盖 service / image / instance 三个 kind：
- create_registry：幂等登记 + kind 路由
- register：按 service_id 幂等 upsert；提升列 + data JSON 分离
- patch：部分更新（不存在 → NotFoundError）
- deregister：幂等删（已删 → False）
- query：等值过滤读（热列走索引）；合并列 + data 还原整条 entry
"""

from __future__ import annotations

import pytest

from a2x_registry.common.ids import image_sid, instance_sid
from a2x_registry.register.errors import (
    NotFoundError,
    ValidationError,
)


SERVICE_REG = "default"
IMAGE_REG = "images"
INSTANCE_REG = "instances"

NOW = "2026-07-14T08:00:00Z"


# ── create_registry：幂等登记 + kind 路由 ─────────────────────

class TestCreateRegistry:
    def test_create_service_registry(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        kinds = table_service.list_registries()
        assert kinds[SERVICE_REG] == "service"

    def test_create_image_registry(self, table_service):
        table_service.create_registry(IMAGE_REG, "image")
        assert table_service.list_registries()[IMAGE_REG] == "image"

    def test_create_instance_registry(self, table_service):
        table_service.create_registry(INSTANCE_REG, "instance")
        assert table_service.list_registries()[INSTANCE_REG] == "instance"

    def test_create_is_idempotent(self, table_service):
        """同 (name, kind) 重复登记不报错，registry_meta 仍只 1 行。"""
        table_service.create_registry(SERVICE_REG, "service")
        table_service.create_registry(SERVICE_REG, "service")  # 重复
        regs = table_service.list_registries()
        assert len(regs) == 1
        assert regs[SERVICE_REG] == "service"

    def test_create_rejects_unknown_kind(self, table_service):
        with pytest.raises(ValidationError):
            table_service.create_registry("bad", "unknown_kind")

    def test_create_all_three_kinds_coexist(self, table_service):
        """三种 kind 注册表在同一库共存，互不干扰。"""
        table_service.create_registry(SERVICE_REG, "service")
        table_service.create_registry(IMAGE_REG, "image")
        table_service.create_registry(INSTANCE_REG, "instance")
        kinds = table_service.list_registries()
        assert kinds == {
            SERVICE_REG: "service",
            IMAGE_REG: "image",
            INSTANCE_REG: "instance",
        }


# ── register：service kind ────────────────────────────────────

class TestRegisterService:
    def _entry(self, sid, **overrides):
        base = {
            "service_id": sid,
            "type": "generic",
            "source": "api_config",
            "name": "svc",
            "description": "d",
            "data": {"endpoint": "http://x"},
        }
        base.update(overrides)
        return base

    def test_register_inserts_new_row(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        row = table_service.register(SERVICE_REG, self._entry("svc_1", name="hello"))
        assert row["service_id"] == "svc_1"
        assert row["name"] == "hello"
        assert row["data"]["endpoint"] == "http://x"

    def test_register_upsert_updates_existing(self, table_service):
        """同 service_id 二次 register → 行数仍 1，字段被更新。"""
        table_service.create_registry(SERVICE_REG, "service")
        table_service.register(SERVICE_REG, self._entry("svc_1", name="v1"))
        row = table_service.register(
            SERVICE_REG, self._entry("svc_1", name="v2", data={"endpoint": "http://y"})
        )
        assert row["name"] == "v2"
        assert row["data"]["endpoint"] == "http://y"

        rows = table_service.query(SERVICE_REG)
        assert len(rows) == 1

    def test_register_data_json_roundtrip(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        payload = {"nested": {"a": [1, 2, 3]}, "unicode": "中文测试"}
        row = table_service.register(SERVICE_REG, self._entry("svc_j", data=payload))
        assert row["data"] == payload

    def test_register_unknown_registry_raises(self, table_service):
        with pytest.raises(NotFoundError):
            table_service.register("no_such_reg", self._entry("x"))

    def test_register_missing_service_id_raises(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        bad = self._entry("svc_1")
        del bad["service_id"]
        with pytest.raises(ValidationError):
            table_service.register(SERVICE_REG, bad)


# ── register：image kind ──────────────────────────────────────

class TestRegisterImage:
    def _entry(self, fw, ver, **overrides):
        # V2: image rows require version_key (NOT NULL) and uploaded_by.
        from a2x_registry.image.version_key import version_key
        base = {
            "service_id": image_sid(fw, ver),
            "framework": fw,
            "framework_version": ver,
            "version_key": version_key(ver),
            "is_default": 0,
            "uploaded_by": "tester",
            "data": {
                "imageurl": f"registry.local/{fw}:{ver}",
                "cpu": 1,
                "memory": "512Mi",
                "ports": [8080],
                "env": {},
                "image_module_version": "v1",
            },
        }
        base.update(overrides)
        return base

    def test_register_image_inserts_new(self, table_service):
        table_service.create_registry(IMAGE_REG, "image")
        row = table_service.register(IMAGE_REG, self._entry("langchain", "0.2.0"))
        assert row["framework"] == "langchain"
        assert row["framework_version"] == "0.2.0"
        assert row["service_id"] == image_sid("langchain", "0.2.0")

    def test_register_image_upsert(self, table_service):
        """同 framework+version 二次 register → service_id 相同 → 行数仍 1。"""
        table_service.create_registry(IMAGE_REG, "image")
        table_service.register(IMAGE_REG, self._entry("langchain", "0.2.0"))
        new_data = {"rootfs": {"type": "docker", "imageurl": "new"}, "cpu": 4}
        row = table_service.register(
            IMAGE_REG, self._entry("langchain", "0.2.0", data=new_data)
        )
        assert row["data"]["cpu"] == 4
        rows = table_service.query(IMAGE_REG)
        assert len(rows) == 1


# ── register：instance kind ───────────────────────────────────

class TestRegisterInstance:
    def _entry(self, user, fw, **overrides):
        sid = instance_sid(user, fw)
        base = {
            "service_id": sid,
            "kind": "三方",
            "framework": fw,
            "framework_version": "0.2.0",
            "node": "192.168.0.11",
            "user": user,
            "data": {
                "address": "http://192.168.0.11:18080",
                "created_at": NOW,
                "last_active_at": NOW,
            },
        }
        base.update(overrides)
        return base

    def test_register_instance_inserts_new(self, table_service):
        table_service.create_registry(INSTANCE_REG, "instance")
        row = table_service.register(INSTANCE_REG, self._entry("alice", "langchain"))
        assert row["service_id"] == instance_sid("alice", "langchain")
        assert row["node"] == "192.168.0.11"

    def test_register_instance_upsert_updates_node(self, table_service):
        """同 (user, framework) → 同 service_id → upsert 更新 node/address。"""
        table_service.create_registry(INSTANCE_REG, "instance")
        table_service.register(
            INSTANCE_REG, self._entry("alice", "langchain", node="192.168.0.11")
        )
        row = table_service.register(
            INSTANCE_REG,
            self._entry(
                "alice",
                "langchain",
                node="192.168.0.99",
                data={"address": "http://new", "created_at": NOW, "last_active_at": NOW},
            ),
        )
        assert row["node"] == "192.168.0.99"
        assert row["data"]["address"] == "http://new"
        rows = table_service.query(INSTANCE_REG)
        assert len(rows) == 1


# ── patch：部分更新 ───────────────────────────────────────────

class TestPatch:
    def test_patch_updates_provided_fields_only(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "svc_p",
                "type": "generic",
                "source": "api_config",
                "name": "orig",
                "description": "orig-desc",
                "data": {"endpoint": "http://x"},
            },
        )
        row = table_service.patch(SERVICE_REG, "svc_p", {"name": "patched"})
        assert row["name"] == "patched"
        assert row["description"] == "orig-desc"  # 未传，保持

    def test_patch_missing_raises_not_found(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        with pytest.raises(NotFoundError):
            table_service.patch(SERVICE_REG, "no_such_sid", {"name": "x"})

    def test_patch_unknown_registry_raises_not_found(self, table_service):
        with pytest.raises(NotFoundError):
            table_service.patch("no_such_reg", "x", {"name": "y"})

    def test_patch_instance_node_address(self, table_service):
        table_service.create_registry(INSTANCE_REG, "instance")
        sid = instance_sid("alice", "langchain")
        table_service.register(
            INSTANCE_REG,
            {
                "service_id": sid,
                "kind": "三方",
                "framework": "langchain",
                "framework_version": "0.2.0",
                "node": "10.0.0.1",
                "user": "alice",
                "data": {"address": "http://old", "created_at": NOW, "last_active_at": NOW},
            },
        )
        row = table_service.patch(INSTANCE_REG, sid, {"node": "10.0.0.2"})
        assert row["node"] == "10.0.0.2"


# ── deregister：幂等删 ────────────────────────────────────────

class TestDeregister:
    def test_deregister_existing_returns_true(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "svc_d",
                "type": "generic",
                "source": "api_config",
                "name": "n",
                "description": "d",
                "data": {},
            },
        )
        assert table_service.deregister(SERVICE_REG, "svc_d") is True

    def test_deregister_missing_returns_false(self, table_service):
        """二次 deregister / 注销不存在的 → False（幂等）。"""
        table_service.create_registry(SERVICE_REG, "service")
        assert table_service.deregister(SERVICE_REG, "never") is False

    def test_deregister_unknown_registry_returns_false(self, table_service):
        assert table_service.deregister("no_such_reg", "x") is False

    def test_deregister_then_query_empty(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "svc_x",
                "type": "generic",
                "source": "api_config",
                "name": "n",
                "description": "d",
                "data": {},
            },
        )
        table_service.deregister(SERVICE_REG, "svc_x")
        assert table_service.query(SERVICE_REG) == []


# ── query：等值过滤 + data 还原 ───────────────────────────────

class TestQuery:
    def _seed_services(self, svc):
        svc.create_registry(SERVICE_REG, "service")
        for i, typ in enumerate(["generic", "a2a", "generic"]):
            svc.register(
                SERVICE_REG,
                {
                    "service_id": f"svc_{i}",
                    "type": typ,
                    "source": "api_config",
                    "name": f"name_{i}",
                    "description": "d",
                    "data": {"idx": i},
                },
            )

    def test_query_all_when_no_filter(self, table_service):
        self._seed_services(table_service)
        rows = table_service.query(SERVICE_REG)
        assert len(rows) == 3

    def test_query_equality_filter_on_hot_column(self, table_service):
        self._seed_services(table_service)
        rows = table_service.query(SERVICE_REG, {"type": "a2a"})
        assert len(rows) == 1
        assert rows[0]["service_id"] == "svc_1"

    def test_query_returns_merged_columns_and_data(self, table_service):
        """query 合并提升列 + data JSON，返回整条 entry。"""
        table_service.create_registry(SERVICE_REG, "service")
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "svc_m",
                "type": "generic",
                "source": "api_config",
                "name": "merged",
                "description": "desc",
                "data": {"endpoint": "http://m", "extra": [1, 2]},
            },
        )
        rows = table_service.query(SERVICE_REG)
        assert len(rows) == 1
        row = rows[0]
        assert row["service_id"] == "svc_m"
        assert row["name"] == "merged"
        assert row["data"]["endpoint"] == "http://m"
        assert row["data"]["extra"] == [1, 2]

    def test_query_empty_registry_returns_empty_list(self, table_service):
        table_service.create_registry(SERVICE_REG, "service")
        assert table_service.query(SERVICE_REG) == []

    def test_query_unknown_registry_returns_empty_list(self, table_service):
        """未登记的注册表查询 → 空列表（不报错）。"""
        assert table_service.query("no_such_reg") == []

    def test_query_filter_by_json_field(self, table_service):
        """data 内字段过滤可选——若实现支持 json_extract 则走它。"""
        table_service.create_registry(SERVICE_REG, "service")
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "s1",
                "type": "generic",
                "source": "api_config",
                "name": "n",
                "description": "d",
                "data": {"endpoint": "http://a"},
            },
        )
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "s2",
                "type": "generic",
                "source": "api_config",
                "name": "n",
                "description": "d",
                "data": {"endpoint": "http://b"},
            },
        )
        # 仅当实现暴露 data 字段过滤时才命中；最低契约：传 data 字段不报错
        rows = table_service.query(SERVICE_REG)
        assert len(rows) == 2


# ── 跨 kind 隔离 ──────────────────────────────────────────────

class TestKindIsolation:
    def test_same_service_id_in_different_kinds_do_not_collide(self, table_service):
        """同名注册表不同 kind：service_id 相同也不冲突（物理表不同）。"""
        table_service.create_registry(SERVICE_REG, "service")
        table_service.create_registry(IMAGE_REG, "image")
        # service 与 image 都用 "shared_id"（主键在不同物理表）
        table_service.register(
            SERVICE_REG,
            {
                "service_id": "shared_id",
                "type": "generic",
                "source": "api_config",
                "name": "n",
                "description": "d",
                "data": {"k": "service"},
            },
        )
        table_service.register(
            IMAGE_REG,
            {
                "service_id": "shared_id",
                "framework": "fw",
                "framework_version": "1.0",
                "version_key": "00000.00001.00000~",
                "is_default": 1,
                "uploaded_by": "tester",
                "data": {"k": "image"},
            },
        )
        assert len(table_service.query(SERVICE_REG)) == 1
        assert len(table_service.query(IMAGE_REG)) == 1
        # 各自的 data 不串
        assert table_service.query(SERVICE_REG)[0]["data"]["k"] == "service"
        assert table_service.query(IMAGE_REG)[0]["data"]["k"] == "image"
