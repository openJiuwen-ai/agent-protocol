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

from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


class RegisterInstanceRequest(BaseModel):
    """``POST /api/instances`` request body（模式 A：仅登记，向前兼容）.

    All fields except ``instance_id`` / ``image_name`` are provided by the
    gateway; ``service_id`` is derived from (user, framework) by the gateway
    for idempotent upsert. ``instance_id`` is the 元戎 instance ID —
    optional (empty when the instance was not launched via 元戎).
    ``image_name`` is the image primary-key reference used by the image
    in-use check (optional; legacy rows fall back to framework matching).
    """

    service_id: str = Field(..., description="user+framework 派生（幂等）")
    kind: str = Field(..., description="实例种类：三方 / 九问")
    framework: str = Field(..., description="框架名")
    framework_version: str = Field(..., description="框架版本")
    image_name: Optional[str] = Field(
        None, description="镜像主键 name（可选；空则镜像在用校验退回 framework 匹配）"
    )
    node: str = Field(..., description="元戎落点 nodeIP")
    instance_id: Optional[str] = Field(
        None, description="元戎实例 ID（gateway 拉起后回填；非元戎拉起可空、不做主键）"
    )
    address: str = Field(..., description="实例访问地址 (IP:port)")
    user: str = Field(..., description="创建 / 所属用户")


class CreateInstanceRuntimeRequest(BaseModel):
    """``POST /api/instances`` request body（模式 B：registry 调元戎创建）.

    = 元戎 create payload + version。``service_id = name``（首个 ``+`` 拆
    user/framework）、``kind`` 由 framework 判、``node``/``address``/
    ``instance_id`` 元戎回填。**有无 runtime_spec 是与模式 A 的判据**。
    实例行冗余存 ``image_name``（镜像主键引用），供镜像注销在用校验按主键关联。
    """

    name: str = Field(..., description="实例主键 = user+framework（= 元戎 name）")
    workspace: str = Field(..., description="绝对路径")
    version: str = Field(..., description="镜像版本（注册中心专属，注销校验用）")
    image_name: Optional[str] = Field(
        None, description="镜像主键 name（在用校验按主键关联的引用键）"
    )
    namespace: Optional[str] = Field(
        None, description="元戎命名空间（可选；registry 配置优先，入参被忽略）"
    )
    runtime_spec: Dict[str, Any] = Field(
        ..., description="元戎沙箱运行规格（runtime / rootfs.imageurl / cpu / memory…），原样转发"
    )
    env_vars: Optional[Dict[str, str]] = Field(None, description="环境变量")
    mounts: Optional[List[Dict[str, Any]]] = Field(None, description="挂载列表")


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
    image_name: str = ""
    user: str
    created_at: str
    last_active_at: str
    status: str  # 运行 | 停止 | 异常


class InstanceDeleteResponse(BaseModel):
    """注销实例回执。``deleted=False`` 表示条目本就不存在（幂等）。

    ``runtime_deleted`` 仅 ``with_runtime=true``（模式 B）时返回：
    元戎实例已删（含元戎本就不存在的幂等情形）为 True；条目无
    instance_id 仅删条目为 False；模式 A 缺省。
    """

    service_id: str
    deleted: bool
    runtime_deleted: Optional[bool] = None


class BatchDeleteResultItem(BaseModel):
    """ALL / 批量删除的单条结果（部分失败不回滚）。"""

    service_id: str
    deleted: bool
    error: Optional[str] = Field(
        None, description='该条失败原因（如 "in_progress" / 元戎失败信息）；成功时缺省'
    )


class BatchDeleteResponse(BaseModel):
    """DELETE /api/instances/ALL 的批量回执。"""

    total: int
    deleted: int
    results: List[BatchDeleteResultItem]
