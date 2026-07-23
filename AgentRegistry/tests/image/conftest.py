"""镜像管理测试 fixtures。

仅验证 sqlite memory 模式。每个测试拿到独立的 memory backend + schema
+ 镜像/实例注册表，互不干扰。
"""

from __future__ import annotations

import pytest

from a2x_registry.common.db import connect, init_schema
from a2x_registry.register.service import RegistryTableService
from a2x_registry.image.service import ImageService
from a2x_registry.image.deps import set_image_service


@pytest.fixture
def table_svc():
    backend = connect({"kind": "memory"})
    init_schema(backend.conn)
    svc = RegistryTableService(backend)
    svc.create_registry("images", "image")
    svc.create_registry("instances", "instance")
    yield svc


@pytest.fixture
def image_svc(table_svc):
    svc = ImageService(table_svc)
    set_image_service(svc)
    yield svc
    set_image_service(None)


def make_runtime_spec(
    imageurl: str = "harbor.local/adapted/opencode:v0.2.0",
    cpu: int = 1000,
    memory: int = 2048,
) -> dict:
    """构造一个不透明透传的 runtime_spec JSON 对象（元戎 RuntimeSpec 结构）。"""
    return {
        "runtime": "python3.11",
        "sandbox_type": "docker",
        "rootfs": {
            "imageurl": imageurl,
            "user": "agentos",
            "ports": ["tcp:8080"],
        },
        "cpu": cpu,
        "memory": memory,
        "ports": [{"port": 8080, "protocol": "tcp"}],
    }


def make_register_body(
    runtime_spec: dict | None = None,
    env_vars: dict | None = None,
    workspace: str = "/app",
    mounts: list | None = None,
    image_module_version: str = "v1.3",
    uploaded_by: str = "user-01",
) -> dict:
    """构造 POST /api/images 请求体（runtime_spec 透传 + 顶层 env_vars/workspace/mounts）。"""
    if runtime_spec is None:
        runtime_spec = make_runtime_spec()
    if env_vars is None:
        env_vars = {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    if mounts is None:
        mounts = [{"source": "/data/agent", "target": "/data"}]
    return {
        "runtime_spec": runtime_spec,
        "env_vars": env_vars,
        "workspace": workspace,
        "mounts": mounts,
        "image_module_version": image_module_version,
        "uploaded_by": uploaded_by,
    }
