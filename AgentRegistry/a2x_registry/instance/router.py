"""FastAPI router for instance management endpoints.

Routes (mounted at app level, prefix ``/api/instances``):

    POST   /api/instances                  register_instance (gateway)
    GET    /api/instances                  list_instances (user; ?size/?page/?include_unhealthy/?node/?framework/?kind/?user)
    PATCH  /api/instances/{service_id}     update_instance (gateway)
    DELETE /api/instances/{service_id}     deregister_instance (gateway)

``GET /api/instances`` supports pagination (``size`` / ``page``) with
``X-Total-Count`` etc. headers when ``size > 0``.
"""

from __future__ import annotations

import math
import logging
from typing import Optional

from fastapi import APIRouter, HTTPException, Query, Response

from a2x_registry.register.errors import NotFoundError, ValidationError

from .deps import get_instance_service
from .models import (
    InstanceDeleteResponse,
    InstanceEntry,
    RegisterInstanceRequest,
    UpdateInstanceRequest,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/instances", tags=["instance"])


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


@router.post("", response_model=InstanceEntry)
async def register_instance(req: RegisterInstanceRequest):
    svc = _resolve_service()
    try:
        return svc.register_instance(req.model_dump())
    except ValidationError as exc:
        raise HTTPException(status_code=400, detail=str(exc))


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
    """Query instances with optional filters; status is derived per-query."""
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


@router.delete("/{service_id}", response_model=InstanceDeleteResponse)
async def deregister_instance(service_id: str):
    svc = _resolve_service()
    return svc.deregister_instance(service_id)