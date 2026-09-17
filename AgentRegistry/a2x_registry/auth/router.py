"""FastAPI router for ``/api/auth/*`` endpoints.

Every endpoint here lives behind ``_require_initialized_auth`` (mounted as
a router-level dependency) so the entire route group returns 404 cleanly
when auth has not been initialized — letting legacy / unbootstrapped
servers behave as if the auth feature doesn't exist.

Route map:
    GET    /api/auth/whoami            require_principal
    POST   /api/auth/principals        require_admin
    GET    /api/auth/principals        require_admin
    GET    /api/auth/principals/{id}   require_admin
    PATCH  /api/auth/principals/{id}   require_admin
    GET    /api/auth/keys              require_principal (self, or admin sees all)
    POST   /api/auth/keys              require_principal (creates own key)
    DELETE /api/auth/keys/{key_id}     require_principal (own key, or admin)

Plaintext tokens leave this module exactly twice:
    1. Response body of POST /api/auth/principals → ``token`` field
    2. Response body of POST /api/auth/keys → ``token`` field
Other responses (list / get) only return ``key_prefix``. Audit log NEVER
contains plaintext.
"""

from __future__ import annotations

from typing import List, Optional

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from a2x_registry.common.auth_context import AuthContext

from .deps import (
    get_auth_store,
    require_admin,
    require_principal,
)
from .store import AuthStore


# ── Router-level guard ───────────────────────────────────────────────────


async def _require_initialized_auth() -> AuthStore:
    """Reject everything under /api/auth with 404 when not bootstrapped.

    Returning 404 (vs 503 "service unavailable") matches the design's
    backward-compat posture: from the perspective of a registry that
    never ran ``auth init``, this route group simply does not exist.
    Operators see an actionable message in the detail field.
    """
    store = get_auth_store()
    if store is None:
        raise HTTPException(
            status_code=404,
            detail=(
                "Auth is not initialized on this registry. "
                "Run 'a2x-registry auth init' to enable the authentication module."
            ),
        )
    return store


router = APIRouter(
    prefix="/api/auth",
    tags=["auth"],
    dependencies=[Depends(_require_initialized_auth)],
)


# ── Request / response models ────────────────────────────────────────────


class CreatePrincipalRequest(BaseModel):
    handle: str
    role: str                            # admin | provider | user
    # None is only valid for role=admin; provider/user must pass a list (may be empty).
    namespaces: Optional[List[str]] = None
    note: str = ""


class UpdatePrincipalRequest(BaseModel):
    namespaces: Optional[List[str]] = None
    role: Optional[str] = None
    disabled: Optional[bool] = None
    note: Optional[str] = None


class CreateKeyRequest(BaseModel):
    name: str = ""


# ── /whoami ──────────────────────────────────────────────────────────────


@router.get("/whoami")
async def whoami(ctx: AuthContext = Depends(require_principal)):
    """Return the caller's identity. Useful for client `login` verification."""
    store = get_auth_store()
    assert store is not None  # router-level dep guarantees this
    principal = store.get_principal(ctx.principal_id)
    if principal is None:
        # Defensive: an in-flight delete could race here; treat as 401.
        raise HTTPException(401, "Principal not found")
    return {
        "principal_id": principal.id,
        "handle": principal.handle,
        "role": principal.role,
        "namespaces": principal.namespaces,
        "disabled": principal.is_disabled,
    }


# ── /principals (admin) ──────────────────────────────────────────────────


@router.post("/principals", status_code=201)
async def create_principal(
    req: CreatePrincipalRequest,
    ctx: AuthContext = Depends(require_admin),
):
    """Create a new principal + return its first API key plaintext.

    Validation: non-admin roles must list at least one namespace they're
    bound to — and every named namespace must exist (admin can't grant
    access to a dataset that doesn't exist on disk). The latter check is
    deferred to the registry service.
    """
    store = get_auth_store()
    assert store is not None
    if req.role != "admin" and req.namespaces is not None:
        # Verify each namespace exists in the registry. Lazy import to
        # avoid a hard auth→backend dependency at module load. Use the
        # directory-based ``dataset_exists`` rather than
        # ``list_datasets_with_counts`` so brand-new empty datasets
        # (created seconds ago, no service.json yet) still pass the check.
        from a2x_registry.backend.routers.dataset import get_registry_service
        try:
            svc = get_registry_service()
        except Exception:
            svc = None
        if svc is not None:
            unknown = [n for n in req.namespaces if not svc.dataset_exists(n)]
            if unknown:
                raise HTTPException(
                    status_code=400,
                    detail=f"Unknown namespace(s): {unknown}. Create the dataset first.",
                )
    try:
        principal, token = store.create_principal(
            handle=req.handle,
            role=req.role,
            namespaces=req.namespaces,
            note=req.note,
            by=ctx.principal_id,
        )
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc
    # Find the key just created (by far the latest for this principal).
    keys = store.list_keys(principal_id=principal.id)
    key = max(keys, key=lambda k: k.created_at) if keys else None
    return {
        "principal_id": principal.id,
        "handle": principal.handle,
        "role": principal.role,
        "namespaces": principal.namespaces,
        "key_id": key.key_id if key else None,
        "key_prefix": key.key_prefix if key else None,
        # PLAINTEXT — only moment it appears. Caller must surface to operator.
        "token": token,
    }


@router.get("/principals")
async def list_principals(_admin: AuthContext = Depends(require_admin)):
    store = get_auth_store()
    assert store is not None
    return [p.model_dump() for p in store.list_principals()]


@router.get("/principals/{principal_id}")
async def get_principal(
    principal_id: str,
    _admin: AuthContext = Depends(require_admin),
):
    store = get_auth_store()
    assert store is not None
    p = store.get_principal(principal_id)
    if p is None:
        raise HTTPException(404, f"Principal {principal_id!r} not found")
    return p.model_dump()


@router.patch("/principals/{principal_id}")
async def update_principal(
    principal_id: str,
    req: UpdatePrincipalRequest,
    ctx: AuthContext = Depends(require_admin),
):
    store = get_auth_store()
    assert store is not None
    # Use the sentinel pattern from AuthStore to distinguish "namespaces
    # not provided" (keep existing) from "set to None" (admin grant-all).
    kwargs = {"by": ctx.principal_id}
    if "namespaces" in req.model_fields_set:
        kwargs["namespaces"] = req.namespaces
    if req.role is not None:
        kwargs["role"] = req.role
    if req.disabled is not None:
        kwargs["disabled"] = req.disabled
    if req.note is not None:
        kwargs["note"] = req.note
    try:
        updated = store.update_principal(principal_id, **kwargs)
    except KeyError as exc:
        raise HTTPException(404, str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc
    return updated.model_dump()


# ── /keys (self + admin) ─────────────────────────────────────────────────


@router.get("/keys")
async def list_keys(
    principal_id: Optional[str] = None,
    ctx: AuthContext = Depends(require_principal),
):
    """List keys.

    - admin: all keys, or filter via ``?principal_id=...``
    - non-admin: their own keys only; ``?principal_id=`` is silently ignored
      when it doesn't match their own id (no info-leak on other principals)
    """
    store = get_auth_store()
    assert store is not None
    if not ctx.is_admin:
        principal_id = ctx.principal_id
    keys = store.list_keys(principal_id=principal_id)
    return [k.model_dump() for k in keys]


@router.post("/keys", status_code=201)
async def create_key(
    req: CreateKeyRequest,
    ctx: AuthContext = Depends(require_principal),
):
    """Create a new key for the caller. Returns plaintext ONCE."""
    store = get_auth_store()
    assert store is not None
    key, token = store.create_key(ctx.principal_id, name=req.name, by=ctx.principal_id)
    return {
        "key_id": key.key_id,
        "principal_id": key.principal_id,
        "key_prefix": key.key_prefix,
        "name": key.name,
        "created_at": key.created_at,
        # PLAINTEXT — only moment it appears.
        "token": token,
    }


@router.delete("/keys/{key_id}")
async def revoke_key(
    key_id: str,
    ctx: AuthContext = Depends(require_principal),
):
    """Revoke a key. Caller must own the key or be admin."""
    store = get_auth_store()
    assert store is not None
    # Authorization: must be owner or admin
    keys = store.list_keys()  # small in-memory list; no concern
    target = next((k for k in keys if k.key_id == key_id), None)
    if target is None:
        raise HTTPException(404, f"Key {key_id!r} not found")
    if not ctx.is_admin and target.principal_id != ctx.principal_id:
        store.audit(
            "permission.denied", reason="not_key_owner",
            principal_id=ctx.principal_id, key_id=key_id,
        )
        raise HTTPException(403, "You can only revoke your own keys")
    try:
        revoked = store.revoke_key(key_id, by=ctx.principal_id)
    except KeyError as exc:
        raise HTTPException(404, str(exc)) from exc
    return {"key_id": revoked.key_id, "revoked_at": revoked.revoked_at}
