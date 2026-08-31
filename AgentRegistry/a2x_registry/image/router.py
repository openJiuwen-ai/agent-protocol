"""FastAPI router for image management endpoints (name 主键).

Routes (mounted at app level, prefix ``/api/images``):

    POST   /api/images                        register_image (image-processing module)
    GET    /api/images                        query (user; flat, ?name / ?framework / ?uploaded_by / ?size / ?page)
    GET    /api/images/{name}/launch-spec     resolve_launch_spec (gateway)
    PUT    /api/images/{name}/default          set_default (user)
    DELETE /api/images/{name}/{version}       deregister (user; 409 if in use)

``name`` is the image primary key (the old ``{framework}`` paths are gone);
``framework`` remains a plain display filter. query returns flat rows (not
grouped by name). Pagination headers (``X-Total-Count`` etc.) are set when
``size > 0``.
"""

from __future__ import annotations

import math
import logging
from typing import Optional

from fastapi import APIRouter, HTTPException, Query, Response

from a2x_registry.register.errors import (
    NotFoundError,
    ValidationError,
)
from a2x_registry.register.errors import ImageInUseError, ExternalDependencyError

from .deps import get_image_service
from .models import (
    DeregisterResponse,
    ImageEntry,
    ImageRegisterResponse,
    LaunchSpecResponse,
    RegisterImageRequest,
    SetDefaultRequest,
    SetDefaultResponse,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/images", tags=["image"])


def _resolve_service():
    svc = get_image_service()
    if svc is None:
        raise HTTPException(
            status_code=404,
            detail=(
                "Image module not assembled (non-appliance mode). "
                "Set A2X_REGISTRY_MODE=appliance to enable."
            ),
        )
    return svc


@router.post("", response_model=ImageRegisterResponse)
async def register_image(req: RegisterImageRequest):
    svc = _resolve_service()
    # 过渡期兼容：version 缺省时回退 deprecated framework_version。
    version = req.version or req.framework_version
    try:
        result = svc.register_image(
            name=req.name,
            version=version,
            runtime_spec=req.runtime_spec,
            uploaded_by=req.uploaded_by,
            framework=req.framework,
            description=req.description,
            package_path=req.package_path,
            image_archive_path=req.image_archive_path,
            access_mode=[am.model_dump() for am in req.access_mode],
            env_vars=req.env_vars,
            workspace=req.workspace,
            mounts=req.mounts,
            image_module_version=req.image_module_version,
        )
    except ValidationError as exc:
        raise HTTPException(status_code=400, detail=str(exc))
    return result


@router.get("", response_model=list[ImageEntry])
async def list_images(
    name: Optional[str] = Query(None, description="按镜像主键 name 筛选"),
    framework: Optional[str] = Query(None, description="按 framework 展示字段筛选"),
    uploaded_by: Optional[str] = Query(None),
    size: int = Query(-1, description="Page size; -1 = no pagination"),
    page: int = Query(1, ge=1, description="Page number (1-based)"),
    response: Response = None,  # noqa: B008 - FastAPI injected
):
    """Query images (flat, one row per name version)."""
    svc = _resolve_service()
    rows, total = svc.query(
        name=name,
        framework=framework,
        uploaded_by=uploaded_by,
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


@router.get("/{name}/launch-spec", response_model=LaunchSpecResponse)
async def get_launch_spec(
    name: str,
    version: Optional[str] = Query(None),
):
    svc = _resolve_service()
    try:
        return svc.resolve_launch_spec(name, version=version)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))


@router.put("/{name}/default", response_model=SetDefaultResponse)
async def set_default(name: str, req: SetDefaultRequest):
    svc = _resolve_service()
    version = req.version or req.framework_version
    if not version:
        raise HTTPException(
            status_code=400, detail="version must not be empty"
        )
    try:
        return svc.set_default(name, version)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))


@router.delete("/{name}/{version}", response_model=DeregisterResponse)
async def deregister_image(name: str, version: str):
    svc = _resolve_service()
    try:
        return svc.deregister(name, version)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))
    except ImageInUseError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    except ExternalDependencyError as exc:
        raise HTTPException(status_code=502, detail=str(exc))
