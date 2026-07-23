"""FastAPI router for image management endpoints.

Routes (mounted at app level, prefix ``/api/images``):

    POST   /api/images                       register_image (image-processing module)
    GET    /api/images                       query (user; flat, ?framework / ?uploaded_by / ?size / ?page)
    GET    /api/images/{framework}/launch-spec  resolve_launch_spec (gateway)
    PUT    /api/images/{framework}/default   set_default (user)
    DELETE /api/images/{framework}/{version} deregister (user; 409 if in use)

query returns flat rows (not grouped by framework). Pagination headers
(``X-Total-Count`` etc.) are set when ``size > 0``.
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
    try:
        result = svc.register_image(
            framework=req.framework,
            framework_version=req.framework_version,
            runtime_spec=req.runtime_spec,
            env_vars=req.env_vars,
            workspace=req.workspace,
            mounts=req.mounts,
            image_module_version=req.image_module_version,
            uploaded_by=req.uploaded_by,
        )
    except ValidationError as exc:
        raise HTTPException(status_code=400, detail=str(exc))
    return result


@router.get("", response_model=list[ImageEntry])
async def list_images(
    framework: Optional[str] = Query(None),
    uploaded_by: Optional[str] = Query(None),
    size: int = Query(-1, description="Page size; -1 = no pagination"),
    page: int = Query(1, ge=1, description="Page number (1-based)"),
    response: Response = None,  # noqa: B008 - FastAPI injected
):
    """Query images (flat, one row per framework version)."""
    svc = _resolve_service()
    rows, total = svc.query(
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


@router.get("/{framework}/launch-spec", response_model=LaunchSpecResponse)
async def get_launch_spec(
    framework: str,
    version: Optional[str] = Query(None),
):
    svc = _resolve_service()
    try:
        return svc.resolve_launch_spec(framework, version=version)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))


@router.put("/{framework}/default", response_model=SetDefaultResponse)
async def set_default(framework: str, req: SetDefaultRequest):
    svc = _resolve_service()
    try:
        return svc.set_default(framework, req.framework_version)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))


@router.delete("/{framework}/{version}", response_model=DeregisterResponse)
async def deregister_image(framework: str, version: str):
    svc = _resolve_service()
    try:
        return svc.deregister(framework, version)
    except NotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc))
    except ImageInUseError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    except ExternalDependencyError as exc:
        raise HTTPException(status_code=502, detail=str(exc))