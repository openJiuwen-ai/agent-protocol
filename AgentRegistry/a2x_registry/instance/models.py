"""Instance management pydantic request / response models.

Maps to the OpenAPI schemas (registry_openapi.yaml §/api/instances):
- ``RegisterInstanceRequest``: gateway registers an instance after
  launching it (三方 / 九问 unified flow).
- ``UpdateInstanceRequest``: gateway updates node/address on migration.
- ``InstanceEntry``: full entry with derived ``status`` (运行 / 异常).
- ``InstanceDeleteResponse``: deregister result with ``deleted`` flag.

``status`` is never stored — it is derived per-query from the node
heartbeat (see InstanceService._derive_status).
"""

from __future__ import annotations

from typing import Optional

from pydantic import BaseModel, Field


class RegisterInstanceRequest(BaseModel):
    """``POST /api/instances`` request body.

    All fields are provided by the gateway; ``service_id`` is derived
    from (user, framework) by the gateway for idempotent upsert.
    """

    service_id: str = Field(..., description="user+framework 派生（幂等）")
    kind: str = Field(..., description="实例种类：三方 / 九问")
    framework: str = Field(..., description="框架名")
    framework_version: str = Field(..., description="框架版本")
    node: str = Field(..., description="元戎落点 nodeIP")
    address: str = Field(..., description="实例访问地址 (IP:port)")
    user: str = Field(..., description="创建 / 所属用户")


class UpdateInstanceRequest(BaseModel):
    """``PATCH /api/instances/{service_id}`` request body.

    At least one of node/address must be provided. ``service_id`` is
    immutable (passed via the path).
    """

    node: Optional[str] = Field(None, description="新落点 nodeIP")
    address: Optional[str] = Field(None, description="新访问地址")


class InstanceEntry(BaseModel):
    """实例注册条目（含派生 status）。"""

    service_id: str
    kind: str
    framework: str
    framework_version: str
    node: str
    address: str
    user: str
    created_at: str
    last_active_at: str
    status: str  # 运行 | 异常


class InstanceDeleteResponse(BaseModel):
    """注销实例回执。``deleted=False`` 表示条目本就不存在（幂等）。"""

    service_id: str
    deleted: bool
