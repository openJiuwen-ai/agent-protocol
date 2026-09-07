"""Image management pydantic request / response models.

The image registry is keyed by ``name`` (one row = one ``name``@``version``);
``framework`` is demoted to a plain display field. ``version`` replaces the
old ``framework_version`` (kept as a deprecated transition alias on
requests / entries). New text fields ``description`` / ``package_path`` /
``image_archive_path`` and the ``access_mode`` array (each item declares one
access route: tui / web / …) are stored inside the ``data`` JSON.

``runtime_spec`` remains an opaque JSON object passthrough (no ``ImageSpec``
typed structure). ``env_vars`` / ``workspace`` / ``mounts`` are top-level
fields alongside ``runtime_spec``, aligning with the yuanrong
``CreateAgentRequest`` layout.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


class AccessMode(BaseModel):
    """One access route of an image (e.g. tui / web)."""

    name: str = Field(..., description="接入方式名，如 tui / web")
    port: str = Field(..., description="端口（字符串），如 18789")
    cmd: Optional[str] = Field(None, description="启动命令")


class RegisterImageRequest(BaseModel):
    """``POST /api/images`` request body.

    ``name`` is the primary key (positioning); ``framework`` is a plain
    display field. ``version`` is required by the contract; the deprecated
    ``framework_version`` is accepted as a transition fallback when
    ``version`` is absent.
    """

    name: str = Field(..., description="镜像主键（取代原 framework 定位）")
    framework: Optional[str] = Field(
        None, description="普通展示字段（非主键）"
    )
    description: Optional[str] = Field(None, description="纯文本描述")
    package_path: Optional[str] = Field(None, description="包路径")
    image_archive_path: Optional[str] = Field(None, description="镜像归档路径")
    version: Optional[str] = Field(
        None, description="镜像版本（原 framework_version 更名）"
    )
    framework_version: Optional[str] = Field(
        None,
        description="【待删除】由 version 取代，过渡期兼容（version 缺省时的回退）",
    )
    runtime_spec: Dict[str, Any] = Field(
        ..., description="Opaque yuanrong RuntimeSpec JSON (passthrough)"
    )
    access_mode: List[AccessMode] = Field(
        default_factory=list, description="接入方式数组（如 tui / web）"
    )
    env_vars: Dict[str, str] = Field(
        default_factory=dict, description="Environment variables"
    )
    workspace: Optional[str] = Field(None, description="Working directory")
    mounts: List[Dict[str, Any]] = Field(
        default_factory=list, description="Volume mounts"
    )
    image_module_version: Optional[str] = Field(
        None, description="Image-processing module version"
    )
    uploaded_by: str = Field(..., description="Uploader identity")


class SetDefaultRequest(BaseModel):
    """``PUT /api/images/{name}/default`` request body.

    ``version`` is required; the deprecated ``framework_version`` is
    accepted as a transition fallback.
    """

    version: Optional[str] = Field(None, description="Version to set as default")
    framework_version: Optional[str] = Field(
        None, description="【待删除】由 version 取代，过渡期兼容回退"
    )


class ImageRegisterResponse(BaseModel):
    """``POST /api/images`` receipt (ImageOpResponse 风格，按 name 定位)。"""

    name: str
    framework: Optional[str] = None
    version: str
    status: str  # "registered" | "updated"


class ImageEntry(BaseModel):
    """One flat row from the image registry (one name's one version).

    ``runtime_spec`` is an opaque JSON passthrough; ``description`` /
    ``package_path`` / ``image_archive_path`` / ``access_mode`` are the §6
    additions; ``framework_version`` mirrors ``version`` during the
    transition (deprecated).
    """

    name: str
    framework: Optional[str] = None
    description: Optional[str] = None
    package_path: Optional[str] = None
    image_archive_path: Optional[str] = None
    version: str
    framework_version: Optional[str] = None  # deprecated, == version
    is_default: bool
    runtime_spec: Optional[Dict[str, Any]] = None
    access_mode: List[Dict[str, Any]] = Field(default_factory=list)
    workspace: Optional[str] = None
    mounts: List[Dict[str, Any]] = Field(default_factory=list)
    env_vars: Dict[str, str] = Field(default_factory=dict)
    image_module_version: Optional[str] = None
    uploaded_by: Optional[str] = None
    created_at: Optional[str] = None


class LaunchSpecResponse(BaseModel):
    """``GET /api/images/{name}/launch-spec`` output.

    ``runtime_spec`` is opaque JSON passthrough; ``env_vars`` / ``workspace``
    / ``mounts`` are top-level fields; ``access_mode`` lets the gateway
    pick a port / command to launch with.
    """

    name: str
    framework: Optional[str] = None
    version: str
    runtime_spec: Optional[Dict[str, Any]] = None
    access_mode: List[Dict[str, Any]] = Field(default_factory=list)
    env_vars: Dict[str, str] = Field(default_factory=dict)
    workspace: Optional[str] = None
    mounts: List[Dict[str, Any]] = Field(default_factory=list)
    image_module_version: Optional[str] = None


class DeregisterResponse(BaseModel):
    name: str
    framework: Optional[str] = None
    version: str
    status: str  # "deregistered"


class SetDefaultResponse(BaseModel):
    name: str
    framework: Optional[str] = None
    default: str
    status: str  # "updated"
