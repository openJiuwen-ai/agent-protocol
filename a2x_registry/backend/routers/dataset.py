"""Dataset management API — CRUD, services, taxonomy, embedding config."""

import asyncio
import logging
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query, Request, UploadFile, File
from fastapi.responses import Response
from pydantic import BaseModel

from a2x_registry.auth.deps import (
    authorize, get_auth_store, require_admin, require_admin_or_anon,
    require_admin_strict,
)
from a2x_registry.heartbeat.deps import get_heartbeat_store
from a2x_registry.heartbeat.errors import (
    HeartbeatError, HeartbeatNotSupportedError,
    TTLOutOfRangeError, TTLRequiredError,
)
from a2x_registry.backend.schemas.models import DatasetInfo, DefaultQuery
from a2x_registry.backend.services.search_service import search_service
from a2x_registry.backend.services.taxonomy_service import get_taxonomy_tree
from a2x_registry.backend.default_queries import get_default_queries
from a2x_registry.common.auth_context import AuthContext
from a2x_registry.register.errors import RegistryNotFoundError
from a2x_registry.register.models import (
    RegisterGenericRequest, RegisterA2ARequest,
    RegisterResponse, DeregisterResponse, SkillResponse, UpdateResponse,
)
from a2x_registry.register.service import RegistryService

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/datasets", tags=["datasets"])

_service: Optional[RegistryService] = None
_executor = ThreadPoolExecutor(max_workers=2)


def init_registry_service(database_dir: Path, global_config_path: Optional[Path] = None):
    """Initialize the global RegistryService. Called once from backend startup."""
    global _service
    _service = RegistryService(database_dir, global_config_path)
    return _service


def get_registry_service() -> RegistryService:
    """Return the registry service or raise 503."""
    if _service is None:
        raise HTTPException(status_code=503, detail="Registry service not initialized")
    return _service


async def _run(fn, *args):
    """Run a blocking function in the thread pool, mapping exceptions to HTTP errors.

    Layered error contract (see docs/client_design.md §3.4):
      RegistryNotFoundError → 404   (business "resource doesn't exist")
      HeartbeatError        → 400 + structured body {code, min_ttl?, max_ttl?}
      ValueError            → 400   (validation / forbidden source)
      PermissionError       → 403   (lease held by a different holder)
      FileNotFoundError     → 404   (skill folder missing on disk)
      KeyError              → 404   (legacy fallback for any not-yet-migrated path)

    ``HeartbeatError`` is a ``ValueError`` subclass; the explicit branch
    runs first to attach the structured body that the client SDK uses to
    auto-retry / surface "valid range [min, max]" hints.
    """
    loop = asyncio.get_event_loop()
    try:
        return await loop.run_in_executor(_executor, fn, *args)
    except RegistryNotFoundError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except HeartbeatError as e:
        # Structured body — SDK dispatches by ``code`` to the right error
        # subclass. ``min_ttl`` / ``max_ttl`` present only when relevant.
        body = {"code": e.code, "detail": str(e)}
        if e.min_ttl is not None:
            body["min_ttl"] = e.min_ttl
        if e.max_ttl is not None:
            body["max_ttl"] = e.max_ttl
        raise HTTPException(status_code=400, detail=body)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    except PermissionError as e:
        raise HTTPException(status_code=403, detail=str(e))
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except FileNotFoundError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except Exception as e:
        logger.error("Registry error: %s", e, exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))


# ── Dataset CRUD ──────────────────────────────────────────────────────────────

@router.get("", response_model=list[DatasetInfo])
async def list_datasets():
    """List available datasets with their service and query counts."""
    items = get_registry_service().list_datasets_with_counts()
    return [DatasetInfo(**item) for item in items]


class _InlineLeaseConfig(BaseModel):
    """Optional lease config payload embedded in CreateDatasetRequest.

    Same shape as the standalone ``LeaseConfigRequest`` used by the
    ``POST /{dataset}/lease-config`` endpoint, but inlined here so admin
    can create + configure heartbeat in one HTTP request (symmetric with
    ``auth_required`` already being a top-level field). Validation logic
    (bound sanity) is identical — both paths converge in
    ``RegistryStore.write_lease_config``.
    """

    enabled: bool = True
    min_ttl: int = 10
    max_ttl: int = 3600
    grace_period: int = 300


class CreateDatasetRequest(BaseModel):
    name: str
    # None resolves to vector.utils.embedding.DEFAULT_EMBEDDING_MODEL on the
    # service side — keeps the literal in one place.
    embedding_model: Optional[str] = None
    # Optional {type: min_version} declaring which registration formats this
    # dataset will accept. Missing/None → all three types allowed from v0.0.
    formats: Optional[dict] = None
    # Opt-in: when True, mutations on this dataset will require a valid
    # API key with namespace access (enforced once the auth router lands
    # in Phase D). Default False keeps backward-compat — datasets created
    # without this field stay fully anonymous, identical to today.
    auth_required: bool = False
    # Opt-in inline heartbeat config. When present, equivalent to creating
    # the dataset then immediately POSTing to /{ds}/lease-config — saves
    # one round-trip for the common "set up a fully-configured namespace"
    # admin flow. Default None preserves backward-compat (no heartbeat
    # config; namespace doesn't accept lease_ttl on register).
    lease_config: Optional[_InlineLeaseConfig] = None


@router.post("")
async def create_dataset(
    req: CreateDatasetRequest,
    request: Request,
):
    """Create a new empty dataset directory with embedding + register-format config.

    Auth behavior:
      - ``auth_required=False`` (default): no token check — preserves the
        legacy fully-anonymous behavior for every existing caller.
      - ``auth_required=True``: requires admin token AND a bootstrapped
        auth store. If the store is None we return 409 with an actionable
        message telling the operator how to bootstrap.
    """
    svc = get_registry_service()
    if req.auth_required:
        store = get_auth_store()
        if store is None:
            raise HTTPException(
                status_code=409,
                detail=(
                    "auth_not_initialized: cannot create an auth-required "
                    "namespace before bootstrapping. Run "
                    "'a2x-registry auth init' first."
                ),
            )
        # Inline admin check (FastAPI Depends would require restructuring;
        # the body-driven conditional gating is clearer inline here).
        auth_header = request.headers.get("authorization")
        from a2x_registry.auth.deps import _parse_bearer
        from a2x_registry.auth.errors import AuthenticationError
        token = _parse_bearer(auth_header)
        if token is None:
            raise HTTPException(401, "Admin token required to create auth-required namespace",
                                headers={"WWW-Authenticate": "Bearer"})
        try:
            ctx = store.authenticate(token)
        except AuthenticationError as exc:
            raise HTTPException(401, str(exc), headers={"WWW-Authenticate": "Bearer"}) from exc
        if not ctx.is_admin:
            store.audit("permission.denied", reason="admin_required",
                        principal_id=ctx.principal_id, role=ctx.role)
            raise HTTPException(403, "Admin role required to create auth-required namespace")
    await _run(
        svc.create_dataset,
        req.name, req.embedding_model, req.formats, req.auth_required,
    )
    # Inline lease config: same write path as POST /{ds}/lease-config so
    # bounds validation + cache invalidation are identical. Atomic enough
    # for practical purposes — the dataset directory exists by this point,
    # and a failure mid-write here leaves the namespace as anonymous +
    # heartbeat-disabled (caller can retry the lease-config call later).
    if req.lease_config is not None:
        await _run(
            svc_set_lease_config, req.name,
            req.lease_config.enabled,
            req.lease_config.min_ttl,
            req.lease_config.max_ttl,
            req.lease_config.grace_period,
        )
    # Echo back the *resolved* embedding model (None → default) by re-reading
    # the persisted vector_config — single source of truth for what's on disk.
    persisted = svc.get_vector_config(req.name)
    response = {
        "dataset": req.name,
        "embedding_model": persisted["embedding_model"],
        "formats": svc.get_register_config(req.name),
        "auth_required": svc.is_auth_required(req.name),
        "status": "created",
    }
    # Surface the lease config in the response only if it was set inline —
    # otherwise omit the key entirely so legacy callers see the exact same
    # response shape as before.
    if req.lease_config is not None:
        response["lease_config"] = svc.get_lease_config(req.name)
    return response


# ── Auth config toggle (admin-only, only valid when store is initialized) ──

@router.get("/{dataset}/auth-config")
async def get_auth_config(dataset: str):
    """Read the per-namespace auth flag. Public — no token required."""
    svc = get_registry_service()
    if not (svc._database_dir / dataset).exists():
        raise HTTPException(404, f"Dataset '{dataset}' not found")
    return {"dataset": dataset, "required": svc.is_auth_required(dataset)}


class AuthConfigRequest(BaseModel):
    required: bool


@router.post("/{dataset}/auth-config")
async def set_auth_config(
    dataset: str,
    req: AuthConfigRequest,
    _admin: AuthContext = Depends(require_admin_strict),
):
    """Toggle the per-namespace auth flag. Admin-only.

    Note: turning auth ON for a namespace that already has entries leaves
    those entries with ``owner_id=None`` → only admin can mutate them
    until they're re-registered. Documented in the design as the least-
    surprising semantics for retrofit; admins can ``PATCH`` ownership
    onto specific entries later if needed.
    """
    cfg = await _run(svc_set_auth_config, dataset, req.required)
    return {"dataset": dataset, **cfg}


def svc_set_auth_config(dataset: str, required: bool):
    """Adapter for ``_run`` — keeps the awaited call site short."""
    return get_registry_service().set_auth_config(dataset, required)


# ── Lease (heartbeat) config — public GET, admin-only POST ───────────────

@router.get("/{dataset}/lease-config")
async def get_lease_config(dataset: str):
    """Read the per-namespace heartbeat lease config. Public — no token
    required, so SDKs can detect whether to send ``lease_ttl`` before
    triggering a 400."""
    svc = get_registry_service()
    if not (svc._database_dir / dataset).exists():
        raise HTTPException(404, f"Dataset '{dataset}' not found")
    return {"dataset": dataset, **svc.get_lease_config(dataset)}


class LeaseConfigRequest(BaseModel):
    enabled: bool
    min_ttl: int = 10
    max_ttl: int = 3600
    grace_period: int = 300


@router.post("/{dataset}/lease-config")
async def set_lease_config(
    dataset: str,
    req: LeaseConfigRequest,
    _ctx: Optional[AuthContext] = Depends(require_admin_or_anon),
):
    """Toggle / configure the per-namespace heartbeat lease policy.

    Admin-only when the registry has auth initialized; anon-OK when no
    auth has been bootstrapped (legacy / dev parity — heartbeat shouldn't
    require admins to also enable the auth module first).

    Existing services on the namespace are not retroactively gated: those
    registered before this toggle have ``lease_ttl=None`` (permanent) and
    stay permanent until re-registered with a ttl. Newly-registered
    services from now on get the matrix validation.
    """
    cfg = await _run(
        svc_set_lease_config, dataset,
        req.enabled, req.min_ttl, req.max_ttl, req.grace_period,
    )
    return {"dataset": dataset, **cfg}


def svc_set_lease_config(
    dataset: str, enabled: bool, min_ttl: int, max_ttl: int, grace_period: int,
):
    return get_registry_service().set_lease_config(
        dataset, enabled=enabled, min_ttl=min_ttl,
        max_ttl=max_ttl, grace_period=grace_period,
    )


# ── Registration format config ────────────────────────────────────────────────

class RegisterConfigRequest(BaseModel):
    formats: dict  # {type: min_version} or {type: {"min_version": "v0.0"}}


@router.get("/{dataset}/register-config")
async def get_register_config(
    dataset: str,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Return the per-type ``min_version`` map that gates registration."""
    svc = get_registry_service()
    return {"dataset": dataset, "formats": svc.get_register_config(dataset)}


@router.post("/{dataset}/register-config")
async def set_register_config(
    dataset: str, req: RegisterConfigRequest,
    _ctx: Optional[AuthContext] = Depends(require_admin_or_anon),
):
    """Replace the allowed registration formats for a dataset.

    Body: ``{"formats": {"generic": "v0.0", "a2a": "v1.0", "skill": "v0.0"}}``.
    Unknown types / versions are silently dropped. Empty result → 400.

    Admin-only on auth-required datasets; anon-OK on legacy datasets.
    """
    svc = get_registry_service()
    cfg = await _run(svc.set_register_config, dataset, req.formats)
    return {"dataset": dataset, "formats": cfg}


@router.delete("/{dataset}")
async def delete_dataset(
    dataset: str,
    _ctx: Optional[AuthContext] = Depends(require_admin_or_anon),
):
    """Delete a dataset directory and all associated data.

    Admin-only on auth-required datasets; anon-OK on legacy datasets.
    """
    search_service.purge_dataset(dataset)
    await _run(get_registry_service().delete_dataset, dataset)
    return {"dataset": dataset, "status": "deleted"}


# ── Services (register / deregister / list) ───────────────────────────────────

_RESERVED_QUERY_PARAMS = frozenset({
    "fields", "page", "size", "include_leased", "include_unhealthy",
})
_VALID_FIELDS = frozenset({"brief", "detail"})


_STATUS_DEFAULT = "online"


def _filter_matches(filters: dict, raw: dict) -> bool:
    """Filter-mode AND match with one carve-out: ``status=online`` matches
    when the entry has no ``status`` field at all (default-online rule).

    Every other filter term still requires the field be present in ``raw``
    AND ``str(raw[k]) == v``. The carve-out lets pre-existing services
    registered without a ``status`` field still be discoverable as online.
    """
    for k, v in filters.items():
        if k == "status" and v == _STATUS_DEFAULT and "status" not in raw:
            continue
        if k not in raw or str(raw[k]) != v:
            return False
    return True


def _entry_filter_dict(entry) -> Optional[dict]:
    """Type-specific 'raw' dict used for filter matching on GET /services.

    Returns the untransformed per-type data model — AgentCard for a2a
    (with ``extra=allow`` custom fields like ``endpoint``/``status``
    preserved), GenericServiceData for generic, SkillData for skill. This
    intentionally differs from the ``build_description``-transformed
    ``description`` exposed at the wrapped output's top level, giving
    filter callers predictable equality semantics on what they wrote in.
    """
    if entry.type == "a2a" and entry.agent_card:
        return entry.agent_card.model_dump(exclude_none=True)
    if entry.type == "generic" and entry.service_data:
        return entry.service_data.model_dump()
    if entry.type == "skill" and entry.skill_data:
        return entry.skill_data.model_dump()
    return None


@router.get("/{dataset}/services")
async def list_services(
    dataset: str,
    request: Request,
    response: Response,
    fields: str = Query("detail", description="brief | detail"),
    size: int = Query(-1, ge=-1),
    page: int = Query(1, ge=1),
    include_leased: bool = Query(False, description="include reserved (leased) entries"),
    include_unhealthy: bool = Query(
        False, description="include heartbeat-expired (unhealthy) entries; default false",
    ),
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """List services in a dataset, optionally filtered.

    Query parameters:
      fields=brief|detail   default: detail
        - brief: [{id, name, description}]                     (lightweight)
        - detail: [{id, type, name, description, metadata, source}]  (full info)
      page, size            pagination (size=-1 means all; default)
      include_leased        default false — leased entries are filtered out so
                            list_idle_blank_agents is automatically race-safe.
                            Set true for admin / debug views that need to see
                            reserved agents.
      <other key>=<value>   filter terms — AND semantics, string-coerced equality
                            on the type-specific raw dict (a2a → agent_card,
                            generic → service_data, skill → skill_data).
                            Special carve-out: status=online matches when the
                            entry has no status field at all (default-online).

    Pagination is via response headers (REST convention):
      X-Total-Count, X-Page, X-Total-Pages, X-Page-Size
    Set when size > 0; absent when size = -1 (all returned in one page).

    Always returns a flat JSON array. For single-service fetch use
    GET /api/datasets/{dataset}/services/{service_id} instead.
    """
    if fields not in _VALID_FIELDS:
        raise HTTPException(
            status_code=400,
            detail=f"fields must be one of {sorted(_VALID_FIELDS)}, got {fields!r}",
        )

    filters = {
        k: v for k, v in request.query_params.items()
        if k not in _RESERVED_QUERY_PARAMS
    }
    svc = get_registry_service()

    # Filter+match phase — uniform for both fields=brief and fields=detail
    wrapped_by_id = {s["id"]: s for s in svc.list_services(dataset)}
    matched = []
    for entry in sorted(svc.list_entries(dataset), key=lambda e: e.service_id):
        if not include_leased and svc.is_leased(dataset, entry.service_id):
            continue
        # Skip heartbeat-unhealthy entries by default; ?include_unhealthy=true
        # surfaces them for ops diagnostics. is_unhealthy is a thin dict
        # lookup against HeartbeatStore (or always-False when the heartbeat
        # module isn't loaded — backward compat).
        if not include_unhealthy and svc.is_unhealthy(dataset, entry.service_id):
            continue
        match_dict = _entry_filter_dict(entry)
        if match_dict is None:
            continue
        if filters and not _filter_matches(filters, match_dict):
            continue
        wrapped = wrapped_by_id.get(entry.service_id)
        if wrapped is None:
            continue
        # Attach source so fields=detail can include it
        matched.append({**wrapped, "source": entry.source})

    total = len(matched)

    # Pagination
    if size == -1:
        page_slice = matched
    else:
        offset = (page - 1) * size
        page_slice = matched[offset: offset + size]
        total_pages = max(1, (total + size - 1) // size)
        response.headers["X-Total-Count"] = str(total)
        response.headers["X-Page"] = str(page)
        response.headers["X-Total-Pages"] = str(total_pages)
        response.headers["X-Page-Size"] = str(size)

    # Field projection
    if fields == "brief":
        return [
            {"id": s["id"], "name": s["name"], "description": s["description"]}
            for s in page_slice
        ]
    return page_slice  # fields == "detail"


@router.get("/{dataset}/services/{service_id}")
async def get_single_service(
    dataset: str, service_id: str,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Fetch a single service by ID.

    For a2a / generic: returns the full wrapped entry as JSON.
    For skill: returns the skill folder packaged as a ZIP file
               (Content-Type: application/zip, Content-Disposition: attachment).
    Returns 404 if the service doesn't exist.

    Replaces the old ?mode=single&service_id=X query-param style.
    """
    svc = get_registry_service()
    entry = svc.get_entry(dataset, service_id)
    if not entry:
        raise HTTPException(
            status_code=404,
            detail=f"Service '{service_id}' not found in dataset '{dataset}'",
        )
    # Skill type: return ZIP download
    if entry.type == "skill" and entry.skill_data:
        try:
            zip_bytes = svc.get_skill_zip(dataset, entry.skill_data.name)
        except FileNotFoundError:
            raise HTTPException(
                status_code=404,
                detail=f"Skill folder not found: {entry.skill_data.name}",
            )
        return Response(
            content=zip_bytes,
            media_type="application/zip",
            headers={"Content-Disposition": f'attachment; filename="{entry.skill_data.name}.zip"'},
        )
    # Generic/A2A: return full service.json entry
    output = [s for s in svc.list_services(dataset) if s["id"] == service_id]
    return output[0] if output else None


def _validate_lease_request(dataset: str, client_ttl: Optional[int]) -> Optional[int]:
    """Run the heartbeat 4-corner matrix BEFORE registering the service.

    Three outcomes:
    - ``None``: no heartbeat module loaded AND client didn't request a
      lease → permanent service (legacy path; perfectly fine).
    - ``int``: validated ttl ready for ``store.install()`` after register.
    - raises ``HTTPException`` (400 with structured body) on matrix violation.

    Caller is the sync register endpoint; we convert HeartbeatError →
    HTTPException here directly because this function runs outside
    ``_run``'s exception mapper.
    """
    store = get_heartbeat_store()
    try:
        if store is None:
            if client_ttl is None:
                return None  # permanent (heartbeat module not loaded — fine)
            raise HeartbeatNotSupportedError(
                "Heartbeat module not initialized on this registry; "
                "remove 'lease_ttl' from the request."
            )
        return store.validate(dataset, client_ttl)
    except HeartbeatError as exc:
        body = {"code": exc.code, "detail": str(exc)}
        if exc.min_ttl is not None:
            body["min_ttl"] = exc.min_ttl
        if exc.max_ttl is not None:
            body["max_ttl"] = exc.max_ttl
        raise HTTPException(status_code=400, detail=body) from exc


def _install_lease(
    response: RegisterResponse, dataset: str, ttl: Optional[int],
) -> RegisterResponse:
    """Install the validated lease after successful register, and attach
    lease info to the response so clients know the expires_at. No-op
    when ``ttl is None`` (permanent service)."""
    if ttl is None:
        return response
    store = get_heartbeat_store()
    assert store is not None  # would have raised in _validate_lease_request
    import time as _t
    lease = store.install(dataset, response.service_id, ttl)
    response.lease_ttl = lease.ttl_seconds
    response.lease_expires_at = _t.time() + lease.ttl_seconds
    return response


@router.post("/{dataset}/services/generic", response_model=RegisterResponse)
async def register_generic(
    dataset: str, req: RegisterGenericRequest,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Register a generic service.

    ``ctx`` is set by the ``authorize`` dep when the dataset is auth-required;
    None for anon namespaces (backward-compat). The service layer writes
    ``owner_id = ctx.principal_id`` when set, ``None`` otherwise.

    Heartbeat: when ``lease_ttl`` is supplied AND the namespace's
    ``lease_config.enabled=true``, the value must satisfy ``[min_ttl,
    max_ttl]``. Validation happens upfront; install happens after register
    succeeds (so a registration failure doesn't leave a dangling lease).
    """
    req.dataset = dataset
    validated_ttl = _validate_lease_request(dataset, req.lease_ttl)
    # Ensure the entry's persisted lease_ttl matches what the heartbeat
    # store actually installs (None on permanent path, int otherwise).
    req.lease_ttl = validated_ttl
    response = await _run(get_registry_service().register_generic, req, ctx)
    return _install_lease(response, dataset, validated_ttl)


@router.post("/{dataset}/services/a2a", response_model=RegisterResponse)
async def register_a2a(
    dataset: str, req: RegisterA2ARequest,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Register an A2A agent. See ``register_generic`` for heartbeat semantics."""
    req.dataset = dataset
    validated_ttl = _validate_lease_request(dataset, req.lease_ttl)
    req.lease_ttl = validated_ttl
    response = await _run(get_registry_service().register_a2a, req, ctx)
    return _install_lease(response, dataset, validated_ttl)


# Fields a non-admin caller may NEVER write via PUT — server identity
# columns. Filtered before the merge so a provider can't relabel their
# ownership of a service.
_FORBIDDEN_UPDATE_FIELDS = frozenset({"owner_id", "service_id", "type", "source"})


@router.put("/{dataset}/services/{service_id}", response_model=UpdateResponse)
async def update_service(
    dataset: str, service_id: str, updates: dict,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Partially update a service by top-level field upsert.

    Body is an arbitrary ``{field: value}`` dict. Fields not present are
    untouched; matching fields are replaced. No format validation (since
    fields are only added/replaced, not removed). Changing ``name`` or
    ``description`` marks the taxonomy as stale.

    Server identity fields (``owner_id``, ``service_id``, ``type``,
    ``source``) are stripped from the body before merge — providers must
    not be able to relabel ownership or service type via PUT.

    Rejected:
      - user_config-sourced entries (edit user_config.json directly)
      - unknown fields for generic / skill types (strict schema)
      - skill rename whose target folder already exists
    """
    if isinstance(updates, dict):
        updates = {k: v for k, v in updates.items() if k not in _FORBIDDEN_UPDATE_FIELDS}
    return await _run(get_registry_service().update_service,
                      dataset, service_id, updates, ctx)


@router.delete("/{dataset}/services/{service_id}", response_model=DeregisterResponse)
async def deregister(
    dataset: str, service_id: str,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Deregister a service."""
    return await _run(get_registry_service().deregister, dataset, service_id, ctx)


# ── Reservations (in-memory leases) ──────────────────────────────────────────

class ReservationRequest(BaseModel):
    filters: dict = {}
    n: int = 1
    ttl_seconds: int = 30
    holder_id: Optional[str] = None


@router.post("/{dataset}/reservations")
async def create_reservation(
    dataset: str, req: ReservationRequest,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Reserve up to ``n`` services matching ``filters``, locked for ``ttl_seconds``.

    On auth-required namespaces, ``holder_id`` in the request body is
    silently overridden with the caller's principal_id (the service layer
    handles this when ``ctx`` is non-None).

    Body: {filters, n, ttl_seconds, holder_id}
    Resp: {holder_id, ttl_seconds, expires_at_unix, reservations: [...]}
    """
    svc = get_registry_service()
    holder_id, expires_at_unix, claimed = await _run(
        svc.reserve_services,
        dataset, req.filters, req.n, req.ttl_seconds, req.holder_id, ctx,
    )
    return {
        "holder_id": holder_id,
        "ttl_seconds": req.ttl_seconds,
        "expires_at_unix": expires_at_unix,
        "reservations": claimed,
    }


@router.delete("/{dataset}/reservations/{holder_id}")
async def release_reservation_bulk(
    dataset: str, holder_id: str,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Release ALL leases held by ``holder_id`` in this dataset. Idempotent.

    On auth-required namespaces, non-admin callers can only release their
    own holder_id (enforced in the service layer).
    """
    svc = get_registry_service()
    released = await _run(svc.release_reservation, dataset, holder_id, None, ctx)
    return {"released": released}


@router.delete("/{dataset}/reservations/{holder_id}/{service_id}")
async def release_reservation_one(
    dataset: str, holder_id: str, service_id: str,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Release a single sid lease IF held by ``holder_id``.

    - Lease missing → ``released: []`` (idempotent)
    - Lease held by a DIFFERENT holder → ``403`` (via _run mapping)
    - On auth-required namespaces, non-admin caller != path holder → ``403``
    """
    svc = get_registry_service()
    released = await _run(
        svc.release_reservation, dataset, holder_id, [service_id], ctx,
    )
    return {"released": released}


@router.post("/{dataset}/reservations/{holder_id}/extend")
async def extend_reservation(
    dataset: str, holder_id: str, body: dict,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Extend all of holder's leases by ``ttl_seconds``. 404 if none held."""
    ttl = int(body.get("ttl_seconds", 30))
    svc = get_registry_service()
    new_expires_at = await _run(svc.extend_reservation, dataset, holder_id, ttl, ctx)
    return {"expires_at_unix": new_expires_at}


@router.delete("/{dataset}/services/{service_id}/lease")
async def release_lease_self(
    dataset: str, service_id: str,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Release ANY lease on (dataset, service_id) regardless of holder.

    Used by the teammate-self path: the agent itself doesn't know who locked
    it. On anon namespaces, SDK ``_owned`` check is the only gate; on
    auth-required namespaces, the caller must be the entry owner (or admin).
    Idempotent.
    """
    svc = get_registry_service()
    released, prev_holder_id = await _run(
        svc.release_lease_by_sid, dataset, service_id, ctx,
    )
    return {"released": released, "prev_holder_id": prev_holder_id}


# ── Skills (register / download / delete) ─────────────────────────────────────

@router.post("/{dataset}/skills", response_model=SkillResponse)
async def upload_skill(
    dataset: str, file: UploadFile = File(...),
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Upload a skill as a ZIP file. ZIP must contain SKILL.md with valid frontmatter."""
    zip_bytes = await file.read()
    svc = get_registry_service()
    return await _run(svc.register_skill, dataset, zip_bytes, ctx)


@router.delete("/{dataset}/skills/{name}", response_model=SkillResponse)
async def delete_skill(
    dataset: str, name: str,
    ctx: Optional[AuthContext] = Depends(authorize),
):
    """Delete a skill and its registry entry."""
    return await _run(get_registry_service().deregister_skill, dataset, name, ctx)


@router.get("/{dataset}/skills/{name}/download")
async def download_skill(
    dataset: str, name: str,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Download a skill folder as a ZIP file."""
    svc = get_registry_service()
    try:
        zip_bytes = svc.get_skill_zip(dataset, name)
    except FileNotFoundError:
        raise HTTPException(status_code=404, detail=f"Skill '{name}' not found")
    return Response(
        content=zip_bytes,
        media_type="application/zip",
        headers={"Content-Disposition": f'attachment; filename="{name}.zip"'},
    )


# ── Taxonomy ──────────────────────────────────────────────────────────────────

@router.get("/{dataset}/taxonomy")
async def get_taxonomy(
    dataset: str,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Return taxonomy tree structure for D3.js visualization."""
    try:
        return get_taxonomy_tree(dataset)
    except FileNotFoundError:
        raise HTTPException(status_code=404, detail=f"No taxonomy for dataset '{dataset}'")


# ── Default queries ───────────────────────────────────────────────────────────

@router.get("/{dataset}/default-queries")
async def default_queries(
    dataset: str,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Return all default queries for the given dataset in original order.

    Response includes a ``source`` field so the frontend can detect when two
    datasets share the same query pool and skip unnecessary reloads.
    """
    queries, source = get_default_queries(dataset)
    return {"source": source, "queries": [DefaultQuery(**q) for q in queries]}


# ── Embedding / vector config ─────────────────────────────────────────────────

@router.get("/embedding-models")
async def list_embedding_models():
    """Return the list of supported embedding models.

    Lite-safe: reads from the zero-dep ``embedding_constants`` submodule, so
    the route works without ``[vector]`` extras (the model list is just
    metadata; users still need ``[vector]`` to actually run vector search).
    """
    from a2x_registry.vector.utils.embedding_constants import EMBEDDING_MODELS
    return {"models": EMBEDDING_MODELS}


@router.get("/{dataset}/vector-config")
async def get_vector_config(
    dataset: str,
    _ctx: Optional[AuthContext] = Depends(authorize),
):
    """Get the vector (embedding) config for a dataset."""
    cfg = get_registry_service().get_vector_config(dataset)
    return {"dataset": dataset, **cfg}


@router.post("/{dataset}/vector-config")
async def set_vector_config(
    dataset: str, body: dict,
    _ctx: Optional[AuthContext] = Depends(require_admin_or_anon),
):
    """Set the embedding model for a dataset. Triggers vector index rebuild.

    Admin-only on auth-required datasets; anon-OK on legacy datasets.
    """
    svc = get_registry_service()
    cfg = await _run(
        svc.set_vector_config,
        dataset, body.get("embedding_model"), body.get("embedding_dim"),
    )
    search_service.schedule_vector_sync(dataset)
    return {"dataset": dataset, **cfg, "message": "配置已保存，向量索引将在后台重建"}
