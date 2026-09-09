"""FastAPI router for instance management endpoints.

Routes (mounted at app level, prefix ``/api/instances``):

    POST   /api/instances                  create / register instance
                                          （按请求体形状判模式：带 runtime_spec →
                                          模式 B registry 调元戎；带 node/address →
                                          模式 A 仅登记，向前兼容）
    GET    /api/instances                  list_instances (user; ?size/?page/?include_unhealthy/?node/?framework/?kind/?user)
    PATCH  /api/instances/{service_id}     update_instance (gateway)
    DELETE /api/instances/{service_id}     deregister / delete instance
                                          （?with_runtime=true → 模式 B 先调元戎；
                                          {service_id}=ALL → 批量删全部）

``GET /api/instances`` supports pagination (``size`` / ``page``) with
``X-Total-Count`` etc. headers when ``size > 0``.
"""

from __future__ import annotations

import math
import logging
from typing import Optional

from fastapi import APIRouter, HTTPException, Query, Request, Response
from pydantic import ValidationError as PydanticValidationError

from a2x_registry.register.errors import NotFoundError, ValidationError

from .deps import get_instance_service
from .errors import (
    InstanceInProgressError,
    InstanceStoreError,
    RuntimeNotConfiguredError,
    YuanrongFailedError,
    YuanrongTimeoutError,
)
from .models import (
    CreateInstanceRuntimeRequest,
    InstanceEntry,
    RegisterInstanceRequest,
    UpdateInstanceRequest,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/instances", tags=["instance"])

# 模式 B 判据：请求体出现 runtime_spec 即模式 B（即使同时带 node/address 也以 B 为准）。
_RUNTIME_SPEC_KEY = "runtime_spec"
# 元戎类字段：出现这些却缺 runtime_spec → 400（无法按模式 A 解释）。
_YUANRONG_ONLY_KEYS = ("workspace", "version", "env_vars", "mounts", "namespace")
# 模式 A 判据：落点字段。
_PLACEMENT_KEYS = ("node", "address")

# 业务异常 → (HTTP 状态码, 错误码) 映射；body 为 {"detail": {code, detail}}。
_ERROR_STATUS_MAP = (
    (InstanceInProgressError, 409, "in_progress"),
    (RuntimeNotConfiguredError, 501, "runtime_not_configured"),
    (YuanrongFailedError, 502, "yuanrong_failed"),
    (YuanrongTimeoutError, 504, "yuanrong_timeout"),
    (InstanceStoreError, 500, "store_error"),
)


def _resolve_service():
    svc = get_instance_service()
    if svc is None:
        raise HTTPException(
            status_code=404,
            detail=(
                "Instance module not assembled (non-appliance mode). "
                "Set A2X_REGISTRY_MODE=appliance to enable."
            ),
        )
    return svc


def _raise_mapped(exc: Exception) -> None:
    """把实例业务异常翻译成 HTTP 响应（错误码进 body）。"""
    for cls, status, code in _ERROR_STATUS_MAP:
        if isinstance(exc, cls):
            raise HTTPException(
                status_code=status,
                detail={"code": code, "detail": str(exc)},
            )
    if isinstance(exc, NotFoundError):
        raise HTTPException(status_code=404, detail=str(exc))
    if isinstance(exc, ValidationError):
        raise HTTPException(status_code=400, detail=str(exc))
    raise HTTPException(status_code=500, detail=str(exc))


@router.post("", response_model=InstanceEntry)
async def register_instance(request: Request):
    """创建 / 注册实例：按请求体形状判模式（无独立开关字段）。"""
    svc = _resolve_service()
    try:
        body = await request.json()
    except Exception:
        raise HTTPException(status_code=400, detail="request body must be JSON")
    if not isinstance(body, dict):
        raise HTTPException(
            status_code=400, detail="request body must be a JSON object"
        )

    has_runtime_spec = body.get(_RUNTIME_SPEC_KEY) is not None
    has_yuanrong_fields = any(
        body.get(key) is not None for key in _YUANRONG_ONLY_KEYS
    )
    has_placement = any(body.get(key) is not None for key in _PLACEMENT_KEYS)

    if has_runtime_spec:
        # 模式 B：registry 调元戎创建（同时给 node/address 也以 B 为准）
        try:
            req = CreateInstanceRuntimeRequest(**body)
        except PydanticValidationError as exc:
            raise HTTPException(status_code=400, detail=str(exc))
        try:
            return await svc.create_instance_runtime(req.model_dump())
        except Exception as exc:  # noqa: BLE001 - 统一经 _raise_mapped 翻译
            _raise_mapped(exc)

    if has_yuanrong_fields:
        # 出现元戎类字段却缺 runtime_spec → 无法判模式 B，也不符合模式 A
        raise HTTPException(
            status_code=400,
            detail=(
                "runtime_spec is required when yuanrong fields "
                f"({', '.join(_YUANRONG_ONLY_KEYS)}) are present"
            ),
        )

    if has_placement:
        # 模式 A：仅登记（向前兼容）
        try:
            req = RegisterInstanceRequest(**body)
        except PydanticValidationError as exc:
            raise HTTPException(status_code=400, detail=str(exc))
        try:
            return svc.register_instance(req.model_dump())
        except Exception as exc:  # noqa: BLE001 - 统一经 _raise_mapped 翻译
            _raise_mapped(exc)

    raise HTTPException(
        status_code=400,
        detail=(
            "cannot determine mode: provide runtime_spec (mode B) or "
            "node/address (mode A)"
        ),
    )


@router.get("", response_model=list[InstanceEntry])
async def list_instances(
    include_unhealthy: bool = Query(False, description="true 含 异常；默认只回 运行"),
    node: Optional[str] = Query(None),
    framework: Optional[str] = Query(None),
    kind: Optional[str] = Query(None),
    user: Optional[str] = Query(None),
    size: int = Query(-1, description="Page size; -1 = no pagination"),
    page: int = Query(1, ge=1, description="Page number (1-based)"),
    response: Response = None,  # noqa: B008 - FastAPI injected
):
    """Query instances with optional filters; status is persisted per-row."""
    svc = _resolve_service()
    flt = {
        k: v for k, v in {
            "node": node, "framework": framework, "kind": kind, "user": user,
        }.items() if v is not None
    }
    rows, total = svc.list_instances(
        filter=flt or None,
        include_unhealthy=include_unhealthy,
        size=size,
        page=page,
    )
    if size > 0:
        response.headers["X-Total-Count"] = str(total)
        response.headers["X-Page"] = str(page)
        response.headers["X-Total-Pages"] = str(
            max(1, math.ceil(total / size)) if total > 0 else 1
        )
        response.headers["X-Page-Size"] = str(len(rows))
    return rows


@router.patch("/{service_id}", response_model=InstanceEntry)
async def update_instance(service_id: str, req: UpdateInstanceRequest):
    svc = _resolve_service()
    fields = {k: v for k, v in req.model_dump().items() if v is not None}
    try:
        return svc.update_instance(service_id, fields)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))
    except ValidationError as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@router.delete("/{service_id}")
async def deregister_instance(
    service_id: str,
    with_runtime: bool = Query(
        False, description="true → 同步调元戎删除实例；缺省仅删条目"
    ),
):
    """删除实例：单个或 ALL 批量；模式 A（仅条目）或模式 B（先元戎后条目）。"""
    svc = _resolve_service()

    if service_id == "ALL":
        try:
            return await svc.delete_all_instances(with_runtime)
        except Exception as exc:  # noqa: BLE001 - 统一经 _raise_mapped 翻译
            _raise_mapped(exc)

    if with_runtime:
        try:
            return await svc.delete_instance_runtime(service_id)
        except Exception as exc:  # noqa: BLE001 - 统一经 _raise_mapped 翻译
            _raise_mapped(exc)

    return svc.deregister_instance(service_id)
