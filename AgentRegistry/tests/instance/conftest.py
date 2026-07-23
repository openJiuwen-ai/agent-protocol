"""实例管理测试 fixtures。

仅验证 sqlite memory 模式（与 tests/image/ 对齐）。每个测试拿到独立的
memory backend + schema + 实例注册表，互不干扰。
"""

from __future__ import annotations

import pytest

from a2x_registry.common.db import connect, init_schema
from a2x_registry.common.ids import instance_sid
from a2x_registry.register.service import RegistryTableService
from a2x_registry.instance.service import InstanceService
from a2x_registry.instance.deps import set_instance_service


@pytest.fixture
def table_svc():
    """独立的 memory backend + schema + 实例注册表。"""
    backend = connect({"kind": "memory"})
    init_schema(backend.conn)
    svc = RegistryTableService(backend)
    svc.create_registry("instances", "instance")
    yield svc


@pytest.fixture
def instance_svc(table_svc):
    """InstanceService 装配好 table_svc。同时注入全局 deps 供 router 测试。"""
    svc = InstanceService(table_svc)
    set_instance_service(svc)
    yield svc
    set_instance_service(None)


def make_entry(
    service_id: str | None = None,
    user: str = "alice",
    framework: str = "langchain",
    framework_version: str = "0.2.0",
    node: str = "192.168.0.11",
    address: str = "10.244.1.7:4096",
    kind: str = "三方",
) -> dict:
    """构造一个最小可用的注册请求 dict。

    service_id 默认由 (user, framework) 派生（与生产口径一致）。
    """
    if service_id is None:
        service_id = instance_sid(user, framework)
    return {
        "service_id": service_id,
        "kind": kind,
        "framework": framework,
        "framework_version": framework_version,
        "node": node,
        "address": address,
        "user": user,
    }
