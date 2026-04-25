"""Dataset management API — CRUD, services, taxonomy, embedding config."""

import asyncio
import logging
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, HTTPException, Query, Request, UploadFile, File
from fastapi.responses import Response
from pydantic import BaseModel

from a2x_registry.backend.schemas.models import DatasetInfo, DefaultQuery
from a2x_registry.backend.services.search_service import search_service
from a2x_registry.backend.services.taxonomy_service import get_taxonomy_tree
from a2x_registry.backend.default_queries import get_default_queries
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
      ValueError            → 400   (validation / forbidden source)
      PermissionError       → 403   (lease held by a different holder)
      FileNotFoundError     → 404   (skill folder missing on disk)
      KeyError              → 404   (legacy fallback for any not-yet-migrated path)
    """
    loop = asyncio.get_event_loop()
    try:
        return await loop.run_in_executor(_executor, fn, *args)
    except RegistryNotFoundError as e:
        raise HTTPException(status_code=404, detail=str(e))
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


class CreateDatasetRequest(BaseModel):
    name: str
    # None resolves to vector.utils.embedding.DEFAULT_EMBEDDING_MODEL on the
    # service side — keeps the literal in one place.
    embedding_model: Optional[str] = None
    # Optional {type: min_version} declaring which registration formats this
    # dataset will accept. Missing/None → all three types allowed from v0.0.
    formats: Optional[dict] = None


@router.post("")
async def create_dataset(req: CreateDatasetRequest):
    """Create a new empty dataset directory with embedding + register-format config."""
    svc = get_registry_service()
    await _run(svc.create_dataset, req.name, req.embedding_model, req.formats)
    # Echo back the *resolved* embedding model (None → default) by re-reading
    # the persisted vector_config — single source of truth for what's on disk.
    persisted = svc.get_vector_config(req.name)
    return {
        "dataset": req.name,
        "embedding_model": persisted["embedding_model"],
        "formats": svc.get_register_config(req.name),
        "status": "created",
    }


# ── Registration format config ────────────────────────────────────────────────

class RegisterConfigRequest(BaseModel):
    formats: dict  # {type: min_version} or {type: {"min_version": "v0.0"}}


@router.get("/{dataset}/register-config")
async def get_register_config(dataset: str):
    """Return the per-type ``min_version`` map that gates registration."""
    svc = get_registry_service()
    return {"dataset": dataset, "formats": svc.get_register_config(dataset)}


@router.post("/{dataset}/register-config")
async def set_register_config(dataset: str, req: RegisterConfigRequest):
    """Replace the allowed registration formats for a dataset.

    Body: ``{"formats": {"generic": "v0.0", "a2a": "v1.0", "skill": "v0.0"}}``.
    Unknown types / versions are silently dropped. Empty result → 400.
    """
    svc = get_registry_service()
    cfg = await _run(svc.set_register_config, dataset, req.formats)
    return {"dataset": dataset, "formats": cfg}


@router.delete("/{dataset}")
async def delete_dataset(dataset: str):
    """Delete a dataset directory and all associated data."""
    search_service.purge_dataset(dataset)
    await _run(get_registry_service().delete_dataset, dataset)
    return {"dataset": dataset, "status": "deleted"}


# ── Services (register / deregister / list) ───────────────────────────────────

_RESERVED_QUERY_PARAMS = frozenset({"fields", "page", "size", "include_leased"})
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
async def get_single_service(dataset: str, service_id: str):
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


@router.post("/{dataset}/services/generic", response_model=RegisterResponse)
async def register_generic(dataset: str, req: RegisterGenericRequest):
    """Register a generic service."""
    req.dataset = dataset
    return await _run(get_registry_service().register_generic, req)


@router.post("/{dataset}/services/a2a", response_model=RegisterResponse)
async def register_a2a(dataset: str, req: RegisterA2ARequest):
    """Register an A2A agent."""
    req.dataset = dataset
    return await _run(get_registry_service().register_a2a, req)


@router.put("/{dataset}/services/{service_id}", response_model=UpdateResponse)
async def update_service(dataset: str, service_id: str, updates: dict):
    """Partially update a service by top-level field upsert.

    Body is an arbitrary ``{field: value}`` dict. Fields not present are
    untouched; matching fields are replaced. No format validation (since
    fields are only added/replaced, not removed). Changing ``name`` or
    ``description`` marks the taxonomy as stale.

    Rejected:
      - user_config-sourced entries (edit user_config.json directly)
      - unknown fields for generic / skill types (strict schema)
      - skill rename whose target folder already exists
    """
    return await _run(get_registry_service().update_service,
                      dataset, service_id, updates)


@router.delete("/{dataset}/services/{service_id}", response_model=DeregisterResponse)
async def deregister(dataset: str, service_id: str):
    """Deregister a service."""
    return await _run(get_registry_service().deregister, dataset, service_id)


# ── Reservations (in-memory leases) ──────────────────────────────────────────

class ReservationRequest(BaseModel):
    filters: dict = {}
    n: int = 1
    ttl_seconds: int = 30
    holder_id: Optional[str] = None


@router.post("/{dataset}/reservations")
async def create_reservation(dataset: str, req: ReservationRequest):
    """Reserve up to ``n`` services matching ``filters``, locked for ``ttl_seconds``.

    Returns the lease holder_id (caller-supplied or auto-generated) and the
    list of wrapped service entries that were claimed. If fewer than ``n``
    services match (or are unleased), returns whatever was claimed.

    Body: {filters, n, ttl_seconds, holder_id}
    Resp: {holder_id, ttl_seconds, expires_at_unix, reservations: [...]}
    """
    svc = get_registry_service()
    holder_id, expires_at_unix, claimed = await _run(
        svc.reserve_services,
        dataset, req.filters, req.n, req.ttl_seconds, req.holder_id,
    )
    return {
        "holder_id": holder_id,
        "ttl_seconds": req.ttl_seconds,
        "expires_at_unix": expires_at_unix,
        "reservations": claimed,
    }


@router.delete("/{dataset}/reservations/{holder_id}")
async def release_reservation_bulk(dataset: str, holder_id: str):
    """Release ALL leases held by ``holder_id`` in this dataset. Idempotent."""
    svc = get_registry_service()
    released = await _run(svc.release_reservation, dataset, holder_id, None)
    return {"released": released}


@router.delete("/{dataset}/reservations/{holder_id}/{service_id}")
async def release_reservation_one(dataset: str, holder_id: str, service_id: str):
    """Release a single sid lease IF held by ``holder_id``.

    - Lease missing → ``released: []`` (idempotent)
    - Lease held by a DIFFERENT holder → ``403`` (via _run mapping)
    """
    svc = get_registry_service()
    released = await _run(
        svc.release_reservation, dataset, holder_id, [service_id],
    )
    return {"released": released}


@router.post("/{dataset}/reservations/{holder_id}/extend")
async def extend_reservation(
    dataset: str, holder_id: str, body: dict,
):
    """Extend all of holder's leases by ``ttl_seconds``. 404 if none held."""
    ttl = int(body.get("ttl_seconds", 30))
    svc = get_registry_service()
    new_expires_at = await _run(svc.extend_reservation, dataset, holder_id, ttl)
    return {"expires_at_unix": new_expires_at}


@router.delete("/{dataset}/services/{service_id}/lease")
async def release_lease_self(dataset: str, service_id: str):
    """Release ANY lease on (dataset, service_id) regardless of holder.

    Used by the teammate-self path: the agent itself doesn't know who locked
    it. SDK ``_owned`` check is the authorization gate. Idempotent.
    """
    svc = get_registry_service()
    released, prev_holder_id = await _run(
        svc.release_lease_by_sid, dataset, service_id,
    )
    return {"released": released, "prev_holder_id": prev_holder_id}


# ── Skills (register / download / delete) ─────────────────────────────────────

@router.post("/{dataset}/skills", response_model=SkillResponse)
async def upload_skill(dataset: str, file: UploadFile = File(...)):
    """Upload a skill as a ZIP file. ZIP must contain SKILL.md with valid frontmatter."""
    zip_bytes = await file.read()
    svc = get_registry_service()
    return await _run(svc.register_skill, dataset, zip_bytes)


@router.delete("/{dataset}/skills/{name}", response_model=SkillResponse)
async def delete_skill(dataset: str, name: str):
    """Delete a skill and its registry entry."""
    return await _run(get_registry_service().deregister_skill, dataset, name)


@router.get("/{dataset}/skills/{name}/download")
async def download_skill(dataset: str, name: str):
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
async def get_taxonomy(dataset: str):
    """Return taxonomy tree structure for D3.js visualization."""
    try:
        return get_taxonomy_tree(dataset)
    except FileNotFoundError:
        raise HTTPException(status_code=404, detail=f"No taxonomy for dataset '{dataset}'")


# ── Default queries ───────────────────────────────────────────────────────────

@router.get("/{dataset}/default-queries")
async def default_queries(dataset: str):
    """Return all default queries for the given dataset in original order.

    Response includes a ``source`` field so the frontend can detect when two
    datasets share the same query pool and skip unnecessary reloads.
    """
    queries, source = get_default_queries(dataset)
    return {"source": source, "queries": [DefaultQuery(**q) for q in queries]}


# ── Embedding / vector config ─────────────────────────────────────────────────

@router.get("/embedding-models")
async def list_embedding_models():
    """Return the list of supported embedding models."""
    from a2x_registry.vector.utils.embedding import EMBEDDING_MODELS
    return {"models": EMBEDDING_MODELS}


@router.get("/{dataset}/vector-config")
async def get_vector_config(dataset: str):
    """Get the vector (embedding) config for a dataset."""
    cfg = get_registry_service().get_vector_config(dataset)
    return {"dataset": dataset, **cfg}


@router.post("/{dataset}/vector-config")
async def set_vector_config(dataset: str, body: dict):
    """Set the embedding model for a dataset. Triggers vector index rebuild."""
    svc = get_registry_service()
    cfg = await _run(
        svc.set_vector_config,
        dataset, body.get("embedding_model"), body.get("embedding_dim"),
    )
    search_service.schedule_vector_sync(dataset)
    return {"dataset": dataset, **cfg, "message": "配置已保存，向量索引将在后台重建"}
