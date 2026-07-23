"""FastAPI router for heartbeat endpoints.

Mounted at app level (no prefix beyond the dataset path). Routes:

    POST   /api/datasets/{ds}/services/{sid}/heartbeat
           body: {status?: "online"|"busy"|"offline"}
           -> 200 {service_id, expires_at, state}
           -> 404 if no lease (service permanent / never registered with ttl)

    DELETE /api/datasets/{ds}/services/{sid}/heartbeat
           body: {permanent?: bool = false}
           -> 200 {service_id, deleted: bool}
           permanent=false: mark unhealthy, grace window starts
           permanent=true: hard-delete via RegistryService (same path as
                            an admin DELETE /services/{sid})

Both endpoints respect ``Depends(authorize)`` for namespace + auth
checks (mirrors the existing reservation endpoints in dataset.py).

The ``node_router`` exposes the appliance-mode per-node heartbeat
endpoints (no dataset prefix - these are global):

    POST   /api/nodes/{node}/heartbeat
           body: {status?: string} (optional)
           -> 200 {node, state, ttl_seconds, expires_at}
           -> 404 if per-node heartbeat module not assembled
           -> 400 if lease config disabled

    GET    /api/lease-config
           -> 200 {enabled, min_ttl, max_ttl, grace_period}
           -> 404 if per-node heartbeat module not assembled

    POST   /api/lease-config
           body: {enabled, min_ttl, max_ttl, grace_period}
           -> 200 updated config
           -> 404 if per-node heartbeat module not assembled
"""

from __future__ import annotations

import logging
import time as _t
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from a2x_registry.auth.deps import authorize
from a2x_registry.common.auth_context import AuthContext

from .deps import get_heartbeat_store, get_node_heartbeat_manager
from .errors import HeartbeatNotSupportedError

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/datasets", tags=["heartbeat"])
node_router = APIRouter(tags=["heartbeat"])


class HeartbeatRequest(BaseModel):
    # Optional status piggyback — POST /heartbeat with {"status": "busy"}
    # updates agent_card.status in the same RPC. None means "don't touch
    # status; just extend the lease."
    status: Optional[str] = None


class RevokeRequest(BaseModel):
    # Default revoke = mark unhealthy + grace window. ``permanent=true``
    # bypasses grace and hard-deletes immediately (used by clients during
    # known-terminal shutdown).
    permanent: bool = False


def _resolve_store():
    """Return the store or 404 (heartbeat module not initialized).

    404 (vs 503) matches the design's posture: from a non-heartbeat
    registry's perspective these routes simply don't exist. Operators
    seeing this either need to initialize the heartbeat module or are
    hitting the wrong namespace.
    """
    store = get_heartbeat_store()
    if store is None:
        raise HTTPException(
            status_code=404,
            detail=(
                "Heartbeat module not initialized on this registry "
                "(no a2x_registry/heartbeat sweeper running)."
            ),
        )
    return store


@router.post("/{dataset}/services/{service_id}/heartbeat")
async def send_heartbeat(
    dataset: str, service_id: str,
    req: HeartbeatRequest,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Extend the heartbeat lease. Optional ``status`` piggybacks an
    agent_card status update so the client doesn't need a second PUT."""
    store = _resolve_store()
    try:
        lease = store.heartbeat(dataset, service_id)
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc

    # Status piggyback — best-effort; failure shouldn't fail the heartbeat
    # (the lease is already extended, which is the more critical effect).
    if req.status is not None:
        try:
            # Lazy import to avoid pulling FastAPI router into auth module
            from a2x_registry.backend.routers.dataset import get_registry_service
            svc = get_registry_service()
            svc.update_service(
                dataset, service_id, {"status": req.status}, caller=_ctx,
            )
        except Exception as exc:  # noqa: BLE001
            logger.warning(
                "heartbeat: status piggyback failed (%s, %s): %s",
                dataset, service_id, exc,
            )

    return {
        "service_id": service_id,
        "dataset": dataset,
        "state": lease.state.value,
        "ttl_seconds": lease.ttl_seconds,
        # Convert monotonic expires_at to wall-clock unix for the client.
        # Approximation: now_wall + (lease.expires_at - now_monotonic).
        "expires_at": _t.time() + (lease.expires_at - _t.monotonic()),
    }


@router.delete("/{dataset}/services/{service_id}/heartbeat")
async def revoke_heartbeat(
    dataset: str, service_id: str,
    req: RevokeRequest,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Revoke the heartbeat lease (mark unhealthy + grace window) or
    hard-delete the service entirely (``permanent=true``)."""
    store = _resolve_store()
    if req.permanent:
        # Hard delete: drop the lease AND remove the entry. Use the same
        # deregister path admin DELETE goes through, so all the usual
        # cleanups fire (cache invalidation, file rewrite, etc.).
        store.drop(dataset, service_id)
        from a2x_registry.backend.routers.dataset import get_registry_service
        svc = get_registry_service()
        try:
            svc.deregister(dataset, service_id, caller=ctx)
        except Exception as exc:  # business layer error → map to 400 / 404
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"service_id": service_id, "dataset": dataset, "permanent": True}

    # Soft revoke: mark unhealthy, grace window starts.
    found = store.revoke(dataset, service_id, permanent=False)
    if not found:
        raise HTTPException(
            status_code=404,
            detail=f"No heartbeat lease for service {service_id!r} in dataset {dataset!r}",
        )
    return {"service_id": service_id, "dataset": dataset, "permanent": False}


# ── per-node heartbeat (appliance mode) ─────────────────────────


class NodeHeartbeatRequest(BaseModel):
    """``POST /api/nodes/{node}/heartbeat`` body (optional)."""

    status: Optional[str] = None


class LeaseConfigModel(BaseModel):
    """``POST /api/lease-config`` body - matches OpenAPI LeaseConfig."""

    enabled: bool
    min_ttl: int
    max_ttl: int
    grace_period: int


def _resolve_node_manager():
    """Return the per-node HeartbeatManager, or 404 if not assembled.

    404 (vs 503): from a non-appliance registry's perspective these
    routes do not exist, matching the fallback semantics of the
    per-service heartbeat router.
    """
    manager = get_node_heartbeat_manager()
    if manager is None:
        raise HTTPException(
            status_code=404,
            detail=(
                "Per-node heartbeat module not assembled (non-appliance mode). "
                "Set A2X_REGISTRY_MODE=appliance to enable."
            ),
        )
    return manager


@node_router.post("/api/nodes/{node}/heartbeat")
async def node_heartbeat(node: str, req: Optional[NodeHeartbeatRequest] = None):
    """Renew the per-node lease (caller: gateway). Covers all instances
    on that node. First heartbeat installs the lease; subsequent ones
    renew (soft recovery from UNHEALTHY within grace)."""
    manager = _resolve_node_manager()
    try:
        lease = manager.heartbeat(node)
    except HeartbeatNotSupportedError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    return {
        "node": node,
        "state": lease.state.value,
        "ttl_seconds": lease.ttl_seconds,
        "expires_at": _t.time() + (lease.expires_at - _t.monotonic()),
    }


@node_router.get("/api/lease-config")
async def get_lease_config():
    """Read the global per-node lease configuration."""
    manager = _resolve_node_manager()
    cfg = manager.store.get_config()
    return {
        "enabled": cfg.enabled,
        "min_ttl": cfg.min_ttl,
        "max_ttl": cfg.max_ttl,
        "grace_period": cfg.grace_period,
    }


@node_router.post("/api/lease-config")
async def set_lease_config(req: LeaseConfigModel):
    """Update the global per-node lease configuration."""
    manager = _resolve_node_manager()
    manager.store.update_config(
        enabled=req.enabled,
        min_ttl=req.min_ttl,
        max_ttl=req.max_ttl,
        grace_period=req.grace_period,
    )
    cfg = manager.store.get_config()
    return {
        "enabled": cfg.enabled,
        "min_ttl": cfg.min_ttl,
        "max_ttl": cfg.max_ttl,
        "grace_period": cfg.grace_period,
    }
