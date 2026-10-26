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

import re
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field, field_validator, model_validator

# ``name`` / ``version`` are opaque passthrough identifiers, but the API also
# uses them as URL path segments (DELETE /api/images/{name}/{version}, …).
# Keep the check deliberately loose: bound the length and reject only values
# that can never be addressed in a path even after percent-encoding, so every
# accepted row stays deletable later.
#   - "/" is a path separator (Starlette decodes %2F back to "/");
#   - C0 control chars / DEL are not URI-safe;
#   - "." / ".." are normalized away by HTTP clients;
#   - "|" in ``name`` is the separator used by ``image_sid(name, version)``:
#     allowing it would make ("a|b", "c") and ("a", "b|c") collide on the
#     same row id and silently overwrite each other. ``version`` may keep
#     "|" because a name without the separator makes the split unique.
#
# The per-field cap is a generous sanity bound; the real addressability guard
# is the combined worst-case encoded budget below (percent-encoding expands
# every UTF-8 byte to 3 URL chars). Keeping name+version under ~7 KB leaves
# headroom below the common 8 KB request-line limit (nginx default header
# buffer), so even long identifiers remain deletable through an ingress.
_IDENTIFIER_MAX_LEN = 1024
_NAME_ID_SEPARATOR = "|"
_PATH_UNSAFE_RE = re.compile(r"[/\x00-\x1f\x7f]")
_PATH_PREFIX_LEN = len("/api/images//")  # name + version path separators
_PATH_ENCODED_BUDGET = 7000  # worst-case encoded bytes for the full path


def _validate_path_safe(value: Optional[str], field_name: str) -> Optional[str]:
    """Reject only characters that break URL-path addressing (passthrough)."""
    if value is None:
        return None
    if _PATH_UNSAFE_RE.search(value):
        raise ValueError(
            f"{field_name} must not contain '/' or control characters"
        )
    if value in (".", ".."):
        raise ValueError(f"{field_name} must not be '.' or '..'")
    return value


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

    ``name`` / ``version`` remain opaque passthrough values, but they are
    also used as URL path segments, so a loose length + path-safety check is
    applied (they must stay addressable by DELETE / launch-spec).
    """

    name: str = Field(
        ...,
        max_length=_IDENTIFIER_MAX_LEN,
        description=(
            "镜像主键（取代原 framework 定位）；≤1024 字符，"
            "不含 '/'、控制字符或 '|'，其余字符原样透传"
        ),
    )
    framework: Optional[str] = Field(
        None, description="普通展示字段（非主键）"
    )
    description: Optional[str] = Field(None, description="纯文本描述")
    package_path: Optional[str] = Field(None, description="包路径")
    image_archive_path: Optional[str] = Field(None, description="镜像归档路径")
    version: Optional[str] = Field(
        None,
        max_length=_IDENTIFIER_MAX_LEN,
        description=(
            "镜像版本（原 framework_version 更名）；≤1024 字符，"
            "不含 '/' 或控制字符"
        ),
    )
    framework_version: Optional[str] = Field(
        None,
        max_length=_IDENTIFIER_MAX_LEN,
        description=(
            "【待删除】由 version 取代，过渡期兼容（version 缺省时的回退）；"
            "约束同 version"
        ),
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

    @field_validator("name", "version", "framework_version")
    @classmethod
    def _check_identifier(cls, value: Optional[str], info) -> Optional[str]:
        value = _validate_path_safe(value, info.field_name)
        if (
            value is not None
            and info.field_name == "name"
            and _NAME_ID_SEPARATOR in value
        ):
            raise ValueError(
                "name must not contain '|' "
                "(reserved as the image id separator)"
            )
        return value

    @model_validator(mode="after")
    def _check_path_budget(self):
        """Ensure name+version still fit in a request line when encoded.

        Percent-encoding turns each UTF-8 byte into 3 URL chars, so the
        worst-case encoded length is ``3 * len(value.encode())``. Both
        segments share one path, hence the combined budget.
        """
        version = self.version or self.framework_version or ""
        try:
            encoded = 3 * (
                len(self.name.encode("utf-8"))
                + len(version.encode("utf-8"))
            )
        except UnicodeEncodeError:
            raise ValueError(
                "name/version must be valid UTF-8"
            ) from None
        if encoded + _PATH_PREFIX_LEN > _PATH_ENCODED_BUDGET:
            raise ValueError(
                "name + version are too long for the URL path "
                f"(worst-case encoded {encoded + _PATH_PREFIX_LEN} bytes > "
                f"{_PATH_ENCODED_BUDGET}); shorten them"
            )
        return self


class SetDefaultRequest(BaseModel):
    """``PUT /api/images/{name}/default`` request body.

    ``version`` is required; the deprecated ``framework_version`` is
    accepted as a transition fallback.
    """

    version: Optional[str] = Field(None, description="Version to set as default")
    framework_version: Optional[str] = Field(
        None, description="【待删除】由 version 取代，过渡期兼容回退"
    )


class UpdateImageRequest(BaseModel):
    """``PATCH /api/images/{name}/{version}`` request body (§8 部分更新).

    At least one field required. Primary key ``name`` / ``version`` and
    ``is_default`` are **not** patchable here (默认版本走
    ``PUT …/default``); the model simply omits them. ``runtime_spec``
    is an opaque JSON object replaced as a whole.
    """

    framework: Optional[str] = Field(None, description="展示字段（非主键）")
    description: Optional[str] = Field(None, description="纯文本描述")
    package_path: Optional[str] = Field(None, description="包路径")
    image_archive_path: Optional[str] = Field(None, description="镜像归档路径")
    runtime_spec: Optional[Dict[str, Any]] = Field(
        None, description="运行规格（不透明 JSON，整体替换）"
    )
    access_mode: Optional[List[AccessMode]] = Field(
        None, description="接入方式数组（如 tui / web）"
    )
    env_vars: Optional[Dict[str, str]] = Field(None, description="环境变量")
    workspace: Optional[str] = Field(None, description="Working directory")
    mounts: Optional[List[Dict[str, Any]]] = Field(None, description="Volume mounts")


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
