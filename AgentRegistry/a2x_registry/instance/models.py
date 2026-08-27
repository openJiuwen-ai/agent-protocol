"""Instance management pydantic request / response models.

Maps to the OpenAPI schemas (registry_openapi.yaml §/api/instances):
- ``RegisterInstanceRequest``: gateway registers an instance after
  launching it (三方 / 九问 unified flow).
- ``UpdateInstanceRequest``: gateway updates node/address/instance_id on
  migration, or sets ``status`` (运行 / 停止 / 异常) from the 元戎 List.
- ``InstanceEntry``: full entry with ``status`` (运行 / 停止 / 异常).
- ``InstanceDeleteResponse``: deregister result with ``deleted`` flag.

``status`` is persisted inside the ``data`` JSON (default 运行 on
register, written by the gateway via PATCH); when the node heartbeat
marks a node UNHEALTHY the derived status shown is 异常 (see
InstanceService._derive_status). ``instance_id`` is the 元戎 instance
ID backfilled by the gateway — optional (empty when not launched via
元戎) and never a primary key.
"""

from __future__ import annotations

from typing import Optional

from pydantic import BaseModel, Field


class RegisterInstanceRequest(BaseModel):
    """``POST /api/instances`` request body.

    All fields except ``instance_id`` are provided by the gateway;
    ``service_id`` is derived from (user, framework) by the gateway for
    idempotent upsert. ``instance_id`` is the 元戎 instance ID —
    optional (empty when the instance was not launched via 元戎).
    """

    service_id: str = Field(..., description="user+framework 派生（幂等）")
    kind: str = Field(..., description="实例种类：三方 / 九问")
    framework: str = Field(..., description="框架名")
    framework_version: str = Field(..., description="框架版本")
    node: str = Field(..., description="元戎落点 nodeIP")
    instance_id: Optional[str] = Field(
        None, description="元戎实例 ID（gateway 拉起后回填；非元戎拉起可空、不做主键）"
    )
    address: str = Field(..., description="实例访问地址 (IP:port)")
    user: str = Field(..., description="创建 / 所属用户")


class UpdateInstanceRequest(BaseModel):
    """``PATCH /api/instances/{service_id}`` request body.

    At least one of node/address/instance_id/status must be provided.
    ``service_id`` is immutable (passed via the path).
    """

    node: Optional[str] = Field(None, description="新落点 nodeIP")
    address: Optional[str] = Field(None, description="新访问地址")
    instance_id: Optional[str] = Field(
        None, description="元戎实例 ID（元戎迁移后变化时回填）"
    )
    status: Optional[str] = Field(
        None, description="存活状态（gateway 据元戎 List 写入）：运行 / 停止 / 异常"
    )


class InstanceEntry(BaseModel):
    """实例注册条目（含落库 status 与元戎 instance_id）。"""

    service_id: str
    kind: str
    framework: str
    framework_version: str
    node: str
    address: str
    instance_id: str = ""
    user: str
    created_at: str
    last_active_at: str
    status: str  # 运行 | 停止 | 异常


class InstanceDeleteResponse(BaseModel):
    """注销实例回执。``deleted=False`` 表示条目本就不存在（幂等）。"""

    service_id: str
    deleted: bool
