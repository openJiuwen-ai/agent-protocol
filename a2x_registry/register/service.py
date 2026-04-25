"""RegistryService — multi-dataset business logic orchestrator.

Maintains an in-memory merged view per dataset. service.json is pure output.
All mutable state (_entries, _output_cache, _taxonomy_states) is protected by _lock.
"""

import hashlib
import json
import logging
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

from .agent_card import build_description, fetch_agent_card
from .errors import RegistryNotFoundError
from .models import (
    AgentCard,
    DeregisterResponse,
    GenericServiceData,
    RegisterA2ARequest,
    RegisterGenericRequest,
    RegisterResponse,
    RegistryEntry,
    RegistryStatus,
    SkillData,
    SkillResponse,
    TaxonomyState,
    UpdateResponse,
)
from .store import RegistryStore, generate_service_id
from .validation import (
    DEFAULT_ALLOWED_VERSIONS,
    DEFAULT_FORMAT_CONFIG,
    SUPPORTED_SERVICE_TYPES,
    ValidationResult,
    normalize_format_config,
    validate_agent_card,
    validate_service,
)

logger = logging.getLogger(__name__)

USER_CONFIG_FILE = "user_config.json"
API_CONFIG_FILE  = "api_config.json"
BUILD_CONFIG_FILE = "build_config.json"
TAXONOMY_FILE    = "taxonomy.json"

DEFAULT_RESERVATION_TTL = 30  # seconds


@dataclass
class _Lease:
    """In-memory reservation lease — SQS visibility-timeout style.

    Volatile by design: not persisted, cleared on restart (correct: leases
    are short — surviving restart isn't useful). Uses time.monotonic() so
    NTP jumps can't break TTL.
    """
    holder_id: str
    acquired_at: float   # time.monotonic()
    expires_at: float    # time.monotonic() + ttl_seconds


class RegistryService:
    """Multi-dataset registry service.

    Thread-safety: a single _lock protects all mutable state
    (_entries, _output_cache, _taxonomy_states). File I/O is delegated to
    RegistryStore which has its own lock for api_config writes.
    """

    def __init__(
        self,
        database_dir: Path,
        global_config_path: Optional[Path] = None,
        allowed_a2a_versions: Optional[Set[str]] = None,
    ):
        self._database_dir = database_dir
        self._global_config_path = global_config_path
        # Legacy knob kept for callers that still pass an a2a allow-list. When
        # set, it overrides the per-dataset register_config.json for a2a.
        self._allowed_a2a_versions = allowed_a2a_versions
        self._stores: Dict[str, RegistryStore] = {}
        # Protected by _lock:
        self._entries: Dict[str, Dict[str, RegistryEntry]] = {}
        self._output_cache: Dict[str, List[dict]] = {}
        self._taxonomy_states: Dict[str, TaxonomyState] = {}
        # dataset -> {type: min_version}; populated lazily from register_config.json
        self._format_configs: Dict[str, Dict[str, str]] = {}
        self._lock = threading.Lock()
        # Reservation leases — protected by _lock, lazy-swept on access.
        # No background daemon, no disk persistence. Restart clears them.
        self._leases: Dict[Tuple[str, str], _Lease] = {}
        # Optional callback: called with dataset name whenever service.json content changes.
        # Used by SearchService to keep the vector index in sync.
        self._on_service_changed = None

    # -----------------------------------------------------------------------
    # Startup
    # -----------------------------------------------------------------------

    def startup(self) -> Dict[str, TaxonomyState]:
        """Initialize all datasets. Returns {dataset: TaxonomyState}.

        Startup is single-threaded (called once from app.py), so no lock
        contention. We still acquire _lock when writing shared state.
        """
        if self._global_config_path and self._global_config_path.exists():
            self._distribute_global_config()

        datasets = self._discover_datasets()
        logger.info("Discovered %d datasets with registration config: %s", len(datasets), datasets)

        # Phase 1: Load config files and collect URL entries to fetch
        url_entries: List[tuple] = []  # [(dataset, entry), ...]
        for dataset in datasets:
            store = self._get_store(dataset)
            user_entries = store.load_user_config()
            api_entries = store.load_api_config()

            # Merge: api overrides user on same ID; validate entries against the
            # dataset's register_config.json (type allow-list + per-type min_version).
            merged = {}
            for e in user_entries + api_entries:
                vr = self._validate_entry(dataset, e, skip_a2a_url_only=True)
                if vr is not None and not vr.valid:
                    logger.warning("Skipping invalid %s '%s' in %s: %s",
                                   e.type, e.service_id, dataset, "; ".join(vr.errors))
                    continue
                merged[e.service_id] = e
                if e.agent_card_url:
                    url_entries.append((dataset, e))

            # Load skill folders (lowest priority — don't override user_config/api_config)
            skill_entries = store.load_skills()
            for e in skill_entries:
                if e.service_id in merged:
                    continue
                vr = self._validate_entry(dataset, e)
                if vr is not None and not vr.valid:
                    logger.warning("Skipping invalid skill '%s' in %s: %s",
                                   e.service_id, dataset, "; ".join(vr.errors))
                    continue
                merged[e.service_id] = e

            with self._lock:
                self._entries[dataset] = merged

        # Phase 2: Parallel fetch agent_card_urls
        if url_entries:
            self._fetch_agent_cards_parallel(url_entries)
            # Re-validate freshly fetched A2A entries against the dataset's
            # register_config. An entry whose card still fails validation is
            # dropped (matches Phase 1 behavior for inline cards).
            for dataset, entry in url_entries:
                if entry.agent_card is None:
                    continue
                vr = self._validate_entry(dataset, entry)
                if vr is not None and not vr.valid:
                    logger.warning("Dropping invalid A2A '%s' in %s after fetch: %s",
                                   entry.service_id, dataset, "; ".join(vr.errors))
                    with self._lock:
                        self._entries.get(dataset, {}).pop(entry.service_id, None)

        # Phase 3: Generate output and compute taxonomy states
        result = {}
        for dataset in datasets:
            store = self._get_store(dataset)

            # Persist fresh agent_card snapshots
            with self._lock:
                api_entries = [e for e in self._entries[dataset].values() if e.source == "api_config"]
            if api_entries:
                store.save_api_batch(api_entries)

            self._regenerate_output(dataset)
            state = self._init_taxonomy_state(dataset)
            result[dataset] = state

            count = len(self._output_cache.get(dataset, []))
            logger.info("Dataset '%s': %d services, taxonomy=%s", dataset, count, state.value)

        return result

    # -----------------------------------------------------------------------
    # Register
    # -----------------------------------------------------------------------

    def _ensure_dataset_initialized(self, dataset: str) -> None:
        """If ``dataset`` has no ``vector_config.json``, fully initialize it
        with default embedding model + formats. Idempotent and race-safe.

        Lets register_* succeed against a missing namespace without leaving
        a half-formed dataset behind (no vector_config / no register_config).
        Embedding model and formats can be changed later via the dedicated
        ``POST /vector-config`` and ``POST /register-config`` endpoints.
        """
        from a2x_registry.vector.utils.embedding import DEFAULT_EMBEDDING_MODEL
        config_file = self._database_dir / dataset / "vector_config.json"
        if config_file.exists():
            return
        # Idempotent file writes via the same helper create_dataset uses.
        # Tolerates an existing-but-half-formed directory (legacy data).
        self._init_dataset_files(
            dataset, DEFAULT_EMBEDDING_MODEL, dict(DEFAULT_FORMAT_CONFIG),
        )
        logger.info("Auto-initialized dataset '%s' with defaults", dataset)

    def register_generic(self, req: RegisterGenericRequest) -> RegisterResponse:
        """Register a generic service."""
        dataset = req.dataset
        payload = {
            "name": req.name, "description": req.description,
            "url": req.url, "inputSchema": req.inputSchema,
        }
        self._require_valid(dataset, "generic", payload)

        service_id = req.service_id or generate_service_id("generic", req.name)
        entry = RegistryEntry(
            service_id=service_id, type="generic",
            source="api_config" if req.persistent else "ephemeral",
            service_data=GenericServiceData(
                name=req.name, description=req.description,
                inputSchema=req.inputSchema, url=req.url,
            ),
        )
        return self._do_register(dataset, entry, req.persistent)

    def register_a2a(self, req: RegisterA2ARequest) -> RegisterResponse:
        """Register an A2A agent (full card or URL)."""
        if req.agent_card:
            agent_card = req.agent_card
            agent_card_url = None
        elif req.agent_card_url:
            agent_card = fetch_agent_card(req.agent_card_url)
            agent_card_url = req.agent_card_url
        else:
            raise ValueError("Either agent_card or agent_card_url must be provided")

        dataset = req.dataset
        self._require_valid(dataset, "a2a", agent_card)

        service_id = req.service_id or generate_service_id("agent", agent_card.name)
        entry = RegistryEntry(
            service_id=service_id, type="a2a",
            source="api_config" if req.persistent else "ephemeral",
            agent_card=agent_card, agent_card_url=agent_card_url,
        )
        return self._do_register(dataset, entry, req.persistent)

    def register_batch(self, entries: List[RegistryEntry], dataset: str, persistent: bool = True):
        """Register multiple entries at once (single file write)."""
        source = "api_config" if persistent else "ephemeral"
        with self._lock:
            ds = self._entries.setdefault(dataset, {})
            for e in entries:
                copy = e.model_copy(update={"source": source})
                ds[copy.service_id] = copy
            if persistent:
                all_api = [e for e in ds.values() if e.source == "api_config"]

        if persistent and all_api:
            self._get_store(dataset).save_api_batch(all_api)
        self._regenerate_output(dataset)
        self._mark_taxonomy_stale(dataset)

    def register_skill(self, dataset: str, zip_bytes: bytes) -> SkillResponse:
        """Upload a skill ZIP, extract to skills/{name}/, register entry.

        Auto-initializes the dataset with defaults if it doesn't exist
        (same behavior as register_a2a / register_generic).
        """
        self._ensure_dataset_initialized(dataset)
        # Enforce skill allow-list before touching disk — avoids saving a ZIP
        # the dataset will never accept.
        self._require_type_allowed(dataset, "skill")

        store = self._get_store(dataset)
        skill_data = store.save_skill_zip(zip_bytes)

        # Post-extraction re-validation — trivially passes v0.0 (save_skill_zip
        # already enforced name+description) but will flag issues once a
        # skill v1.0+ adds stricter checks.
        self._require_valid(dataset, "skill", skill_data)

        service_id = generate_service_id("skill", skill_data.name)
        entry = RegistryEntry(
            service_id=service_id,
            type="skill",
            source="skill_folder",
            skill_data=skill_data,
        )

        with self._lock:
            ds = self._entries.setdefault(dataset, {})
            status = "updated" if service_id in ds else "registered"
            ds[service_id] = entry

        self._regenerate_output(dataset)
        self._mark_taxonomy_stale(dataset)
        return SkillResponse(name=skill_data.name, dataset=dataset,
                             status=status, service_id=service_id)

    def deregister_skill(self, dataset: str, name: str) -> SkillResponse:
        """Remove a skill folder and its registry entry."""
        service_id = generate_service_id("skill", name)

        with self._lock:
            ds = self._entries.get(dataset, {})
            if service_id not in ds:
                return SkillResponse(name=name, dataset=dataset, status="not_found")
            del ds[service_id]

        store = self._get_store(dataset)
        store.remove_skill(name)
        self._regenerate_output(dataset)
        self._mark_taxonomy_stale(dataset)
        return SkillResponse(name=name, dataset=dataset, status="deleted",
                             service_id=service_id)

    def get_skill_zip(self, dataset: str, name: str) -> bytes:
        """Pack a skill folder into a ZIP and return bytes."""
        return self._get_store(dataset).get_skill_zip(name)

    # -----------------------------------------------------------------------
    # Update (partial field merge)
    # -----------------------------------------------------------------------

    # Fields the update endpoint accepts per type. For a2a we accept anything
    # — the AgentCard model has ``extra="allow"`` and the update contract is
    # "add or replace, never remove". Generic and Skill have strict schemas.
    _GENERIC_UPDATE_FIELDS = {"name", "description", "inputSchema", "url"}
    _SKILL_UPDATE_FIELDS   = {"name", "description", "license"}

    def update_service(self, dataset: str, service_id: str,
                       updates: Dict[str, Any]) -> UpdateResponse:
        """Partially update a service by top-level field upsert.

        Semantics:
          - Each key in ``updates`` replaces the matching field on the entry;
            keys that don't exist yet are added (only for types that accept
            extras, i.e. a2a).
          - No format validation — updates never remove required fields, so the
            original validation guarantees still hold.
          - If ``name`` or ``description`` actually changed, marks the
            dataset's taxonomy STALE so the next search re-evaluates it.
          - Rejects updates to ``user_config`` entries (edit the file instead).
          - For skill entries, persists to ``SKILL.md`` on disk; a name change
            also renames the ``skills/{name}/`` folder.

        Raises:
          RegistryNotFoundError — service_id not found in dataset
          ValueError            — user_config source / unknown fields / rename collision
        """
        if not isinstance(updates, dict):
            raise ValueError("updates must be a dict of {field: value}")

        with self._lock:
            ds = self._entries.get(dataset, {})
            entry = ds.get(service_id)
            if entry is None:
                raise RegistryNotFoundError(
                    f"Service '{service_id}' not found in dataset '{dataset}'"
                )
            if entry.source == "user_config":
                raise ValueError(
                    "Cannot update user_config entries via API. "
                    "Edit user_config.json directly and restart.")

        # Apply the merge outside the lock — disk writes happen here.
        if entry.type == "generic":
            new_entry, changed = self._apply_generic_updates(entry, updates)
        elif entry.type == "a2a":
            new_entry, changed = self._apply_a2a_updates(entry, updates)
        elif entry.type == "skill":
            new_entry, changed = self._apply_skill_updates(dataset, entry, updates)
        else:
            raise ValueError(f"Unsupported entry type: {entry.type!r}")

        # Commit the new in-memory entry. If a concurrent deregister removed
        # the entry during our disk I/O, don't resurrect it — the deregister
        # wins. The already-applied disk side-effects (renamed folder, rewritten
        # SKILL.md) will be reconciled on next startup.
        with self._lock:
            ds = self._entries.get(dataset, {})
            if service_id not in ds:
                raise RegistryNotFoundError(
                    f"Service '{service_id}' was removed during update in dataset '{dataset}'"
                )
            ds[service_id] = new_entry

        # Persist: api_config entries round-trip to disk; skill_folder entries
        # were updated in place during _apply_skill_updates; ephemeral stays
        # in-memory only.
        if new_entry.source == "api_config":
            self._get_store(dataset).save_api_entry(new_entry)

        self._regenerate_output(dataset)

        taxonomy_affected = bool({"name", "description"} & changed)
        if taxonomy_affected:
            self._mark_taxonomy_stale(dataset)

        return UpdateResponse(
            service_id=service_id, dataset=dataset, status="updated",
            changed_fields=sorted(changed), taxonomy_affected=taxonomy_affected)

    def _apply_generic_updates(self, entry: RegistryEntry,
                               updates: Dict[str, Any]):
        unknown = set(updates) - self._GENERIC_UPDATE_FIELDS
        if unknown:
            raise ValueError(
                f"Unknown generic fields: {sorted(unknown)}. "
                f"Allowed: {sorted(self._GENERIC_UPDATE_FIELDS)}")
        current = entry.service_data.model_dump() if entry.service_data else {}
        changed = {k for k, v in updates.items() if current.get(k) != v}
        current.update(updates)
        new_data = GenericServiceData(**current)
        return entry.model_copy(update={"service_data": new_data}), changed

    def _apply_a2a_updates(self, entry: RegistryEntry,
                           updates: Dict[str, Any]):
        if entry.agent_card is None:
            raise ValueError("Cannot update a2a entry with no resolved agent_card")
        current = entry.agent_card.model_dump()
        changed = {k for k, v in updates.items() if current.get(k) != v}
        current.update(updates)
        new_card = AgentCard(**current)
        return entry.model_copy(update={"agent_card": new_card}), changed

    def _apply_skill_updates(self, dataset: str, entry: RegistryEntry,
                             updates: Dict[str, Any]):
        unknown = set(updates) - self._SKILL_UPDATE_FIELDS
        if unknown:
            raise ValueError(
                f"Unknown skill fields: {sorted(unknown)}. "
                f"Allowed: {sorted(self._SKILL_UPDATE_FIELDS)}")
        if entry.skill_data is None:
            raise ValueError("skill entry has no skill_data")

        current = entry.skill_data.model_dump()
        changed = {k for k, v in updates.items() if current.get(k) != v}
        merged = dict(current)
        merged.update(updates)

        store = self._get_store(dataset)
        old_name = current["name"]
        new_name = merged["name"]

        # Rename folder first so subsequent SKILL.md write targets the new path.
        if new_name != old_name:
            store.rename_skill(old_name, new_name)
            merged["skill_path"] = f"skills/{new_name}"

        # Rewrite frontmatter — only the actually-changed fields to preserve
        # any other YAML keys the user may have kept in the file.
        md_updates = {k: merged[k] for k in ("name", "description", "license")
                      if k in changed and merged.get(k) is not None}
        if md_updates:
            store.update_skill_md(new_name, md_updates)

        new_data = SkillData(**merged)
        return entry.model_copy(update={"skill_data": new_data}), changed

    def _do_register(self, dataset: str, entry: RegistryEntry, persistent: bool) -> RegisterResponse:
        """Shared logic for register_generic and register_a2a.

        Auto-initializes the dataset (defaults: all-MiniLM-L6-v2 + all
        formats at v0.0) if no ``vector_config.json`` exists yet, so
        registration to a fresh namespace doesn't leave a half-formed dir.
        """
        self._ensure_dataset_initialized(dataset)
        with self._lock:
            ds = self._entries.setdefault(dataset, {})
            status = "updated" if entry.service_id in ds else "registered"
            ds[entry.service_id] = entry

        if persistent:
            self._get_store(dataset).save_api_entry(entry)
        self._regenerate_output(dataset)
        self._mark_taxonomy_stale(dataset)
        return RegisterResponse(service_id=entry.service_id, dataset=dataset, status=status)

    # -----------------------------------------------------------------------
    # Deregister
    # -----------------------------------------------------------------------

    def deregister(self, dataset: str, service_id: str) -> DeregisterResponse:
        """Deregister a service.

        Raises:
          RegistryNotFoundError — service_id not found in dataset
          ValueError            — entry source is user_config / skill_folder
        """
        with self._lock:
            ds = self._entries.get(dataset, {})
            if service_id not in ds:
                raise RegistryNotFoundError(
                    f"Service '{service_id}' not found in dataset '{dataset}'"
                )

            entry = ds[service_id]
            if entry.source == "user_config":
                raise ValueError("Cannot deregister user_config entries via API. Edit user_config.json instead.")
            if entry.source == "skill_folder":
                raise ValueError("Cannot deregister skill entries via generic API. Use DELETE /skills/{name} instead.")

            source = entry.source
            del ds[service_id]

        if source == "api_config":
            self._get_store(dataset).remove_api_entry(service_id)
        self._regenerate_output(dataset)
        self._mark_taxonomy_stale(dataset)
        return DeregisterResponse(service_id=service_id, status="deregistered")

    # -----------------------------------------------------------------------
    # Taxonomy state
    # -----------------------------------------------------------------------

    def get_taxonomy_state(self, dataset: str) -> Optional[TaxonomyState]:
        """Return cached taxonomy state, or None if dataset is not registry-managed."""
        with self._lock:
            return self._taxonomy_states.get(dataset)

    def check_taxonomy_state(self, dataset: str) -> Optional[TaxonomyState]:
        """Return taxonomy state, resolving STALE by re-checking the hash.

        Returns None for datasets not managed by this registry instance.
        """
        with self._lock:
            state = self._taxonomy_states.get(dataset)
        if state is None:
            return None
        if state != TaxonomyState.STALE:
            return state

        # Stale → recompute
        new_state = self._compute_taxonomy_state(dataset)
        with self._lock:
            self._taxonomy_states[dataset] = new_state
        logger.info("Dataset '%s': taxonomy re-checked, state=%s", dataset, new_state.value)
        return new_state

    # -----------------------------------------------------------------------
    # Query (read-only, return snapshots)
    # -----------------------------------------------------------------------

    def list_services(self, dataset: str) -> List[dict]:
        """Return cached output for a dataset."""
        with self._lock:
            return list(self._output_cache.get(dataset, []))

    def list_entries(self, dataset: str) -> List[RegistryEntry]:
        """Return all RegistryEntry objects for a dataset (includes source info)."""
        with self._lock:
            return list(self._entries.get(dataset, {}).values())

    def get_entry(self, dataset: str, service_id: str) -> Optional[RegistryEntry]:
        """Get a single registry entry."""
        with self._lock:
            return self._entries.get(dataset, {}).get(service_id)

    def get_status(self, dataset: Optional[str] = None) -> RegistryStatus:
        """Get registry status summary."""
        with self._lock:
            datasets_to_check = [dataset] if dataset else list(self._entries.keys())
            total = 0
            by_source: Dict[str, int] = {}
            for ds in datasets_to_check:
                for entry in self._entries.get(ds, {}).values():
                    total += 1
                    by_source[entry.source] = by_source.get(entry.source, 0) + 1
            all_datasets = list(self._entries.keys())

        return RegistryStatus(total_services=total, by_source=by_source, datasets=all_datasets)

    def list_datasets(self) -> List[str]:
        """List all datasets that have registry data."""
        with self._lock:
            return list(self._entries.keys())

    def dataset_dir(self, dataset: str) -> Path:
        """Return the on-disk directory for a dataset.

        Lets callers (build pipeline, search service, frontend export
        helpers) ask "where does this dataset live?" without rebuilding
        the path from PROJECT_ROOT themselves.
        """
        return self._database_dir / dataset

    def service_json_path(self, dataset: str) -> Path:
        """Return the path to ``service.json`` for a dataset (the unified
        output file used by downstream build / search / vector pipelines)."""
        return self.dataset_dir(dataset) / "service.json"

    def query_path(self, dataset: str) -> Path:
        """Path to ``query/query.json`` (evaluation query set)."""
        return self.dataset_dir(dataset) / "query" / "query.json"

    def taxonomy_dir(self, dataset: str) -> Path:
        """Directory holding ``taxonomy.json`` / ``class.json`` etc."""
        return self.dataset_dir(dataset) / "taxonomy"

    def taxonomy_path(self, dataset: str) -> Path:
        """Path to ``taxonomy.json`` (built tree structure)."""
        return self.taxonomy_dir(dataset) / "taxonomy.json"

    def class_path(self, dataset: str) -> Path:
        """Path to ``class.json`` (per-category labels + descriptions)."""
        return self.taxonomy_dir(dataset) / "class.json"

    def chroma_dir(self) -> Path:
        """Shared ChromaDB directory (one per database root, not per dataset)."""
        return self._database_dir / "chroma"

    def list_datasets_with_counts(self) -> List[Dict[str, Any]]:
        """List all datasets on disk with their service + query counts.

        Walks ``self._database_dir`` and returns one dict per dataset
        directory that has a ``service.json`` (i.e., has been registered
        to at least once). Each dict has keys:

          - ``name``: dataset name (directory name)
          - ``service_count``: number of entries in ``service.json``
          - ``query_count``: number of entries in ``query/query.json``
            (0 if the file is missing or malformed)
        """
        if not self._database_dir.exists():
            return []
        out: List[Dict[str, Any]] = []
        for d in sorted(self._database_dir.iterdir()):
            if not d.is_dir():
                continue
            service_file = d / "service.json"
            if not service_file.exists():
                continue
            try:
                svc_count = len(json.loads(service_file.read_text(encoding="utf-8")))
            except (OSError, json.JSONDecodeError):
                svc_count = 0
            query_file = d / "query" / "query.json"
            q_count = 0
            if query_file.exists():
                try:
                    q_count = len(json.loads(query_file.read_text(encoding="utf-8")))
                except (OSError, json.JSONDecodeError):
                    pass
            out.append({
                "name": d.name,
                "service_count": svc_count,
                "query_count": q_count,
            })
        return out

    def set_on_service_changed(self, callback) -> None:
        """Register a callback(dataset: str) invoked when service.json content changes.

        Called after the file is written, from the same thread that triggered the
        change. The callback should be non-blocking (e.g. schedule background work).
        """
        self._on_service_changed = callback

    # -----------------------------------------------------------------------
    # Reservation leases (in-memory, side-table, no disk persistence)
    # -----------------------------------------------------------------------

    def _sweep_expired_leases_locked(self, now: float) -> None:
        """Drop expired leases. Caller must hold _lock."""
        expired = [k for k, lease in self._leases.items() if lease.expires_at <= now]
        for k in expired:
            del self._leases[k]

    def is_leased(self, dataset: str, service_id: str) -> bool:
        """Return True if there's an unexpired lease on (dataset, service_id)."""
        now = time.monotonic()
        with self._lock:
            self._sweep_expired_leases_locked(now)
            return (dataset, service_id) in self._leases

    def reserve_services(
        self,
        dataset: str,
        filters: Dict[str, str],
        n: int,
        ttl_seconds: int,
        holder_id: Optional[str],
    ) -> Tuple[str, float, List[dict]]:
        """Atomically filter-AND-claim up to n unleased matching services.

        Returns (holder_id, expires_at_unix, reservations) where reservations
        is a list of wrapped service entries (same shape as list_services).

        TOCTOU-safe: filter+claim happens under _lock. ``ttl_seconds`` is
        clamped to >= 1.
        """
        if n < 0:
            raise ValueError(f"n must be >= 0, got {n}")
        if ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds}")
        if holder_id is None:
            holder_id = f"holder_{uuid.uuid4().hex}"

        now_mono = time.monotonic()
        now_wall = time.time()
        expires_at_mono = now_mono + ttl_seconds
        expires_at_wall = now_wall + ttl_seconds

        with self._lock:
            self._sweep_expired_leases_locked(now_mono)

            # Build wrapped lookup once for shape-preservation in the response.
            wrapped_by_id = {s["id"]: s for s in self._output_cache.get(dataset, [])}

            claimed: List[dict] = []
            for entry in sorted(
                self._entries.get(dataset, {}).values(),
                key=lambda e: e.service_id,
            ):
                if (dataset, entry.service_id) in self._leases:
                    continue
                if not self._entry_matches_filters(entry, filters):
                    continue
                wrapped = wrapped_by_id.get(entry.service_id)
                if wrapped is None:
                    continue
                claimed.append(wrapped)
                if len(claimed) >= n:
                    break

            for wrapped in claimed:
                self._leases[(dataset, wrapped["id"])] = _Lease(
                    holder_id=holder_id,
                    acquired_at=now_mono,
                    expires_at=expires_at_mono,
                )

        return holder_id, expires_at_wall, claimed

    def _entry_matches_filters(self, entry, filters: Dict[str, str]) -> bool:
        """Match an entry against filter dict using the same semantics as
        the GET /services filter endpoint, including the default-online rule.
        """
        if entry.type == "a2a" and entry.agent_card:
            raw = entry.agent_card.model_dump(exclude_none=True)
        elif entry.type == "generic" and entry.service_data:
            raw = entry.service_data.model_dump()
        elif entry.type == "skill" and entry.skill_data:
            raw = entry.skill_data.model_dump()
        else:
            return False
        for k, v in filters.items():
            if k == "status" and v == "online" and "status" not in raw:
                continue
            if k not in raw or str(raw[k]) != v:
                return False
        return True

    def release_reservation(
        self,
        dataset: str,
        holder_id: str,
        service_ids: Optional[List[str]] = None,
    ) -> List[str]:
        """Release leases held by ``holder_id``.

        - service_ids=None: release ALL leases under holder_id (bulk).
        - service_ids=[...]: release only those sids (per-sid). Sids not held
          by this holder are silently skipped (idempotent).

        Returns the list of sids actually released.

        Raises PermissionError if a requested sid IS leased but under a
        different holder (caller is trying to release someone else's lease).
        """
        with self._lock:
            self._sweep_expired_leases_locked(time.monotonic())
            released: List[str] = []
            if service_ids is None:
                for (ds, sid), lease in list(self._leases.items()):
                    if ds == dataset and lease.holder_id == holder_id:
                        del self._leases[(ds, sid)]
                        released.append(sid)
            else:
                for sid in service_ids:
                    key = (dataset, sid)
                    lease = self._leases.get(key)
                    if lease is None:
                        continue  # idempotent: already gone or never held
                    if lease.holder_id != holder_id:
                        raise PermissionError(
                            f"Lease on '{sid}' is held by a different holder"
                        )
                    del self._leases[key]
                    released.append(sid)
            return released

    def release_lease_by_sid(
        self,
        dataset: str,
        service_id: str,
    ) -> Tuple[bool, Optional[str]]:
        """Release ANY lease on (dataset, service_id) regardless of holder.

        Used by the teammate-self path: the agent itself doesn't know who
        leased it. SDK-level _owned check is the authorization gate.

        Returns (released, prev_holder_id). Idempotent: if no lease exists,
        returns (False, None).
        """
        with self._lock:
            self._sweep_expired_leases_locked(time.monotonic())
            key = (dataset, service_id)
            lease = self._leases.pop(key, None)
            if lease is None:
                return False, None
            return True, lease.holder_id

    def extend_reservation(
        self,
        dataset: str,
        holder_id: str,
        ttl_seconds: int,
    ) -> float:
        """Extend all of holder_id's leases in `dataset` by ttl_seconds.

        Returns the new expires_at_unix (wall-clock, for client display).
        Raises RegistryNotFoundError if holder_id has no live leases (to
        force callers to face the lost-work case explicitly).
        """
        if ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds}")
        now_mono = time.monotonic()
        now_wall = time.time()
        new_mono = now_mono + ttl_seconds
        new_wall = now_wall + ttl_seconds
        with self._lock:
            self._sweep_expired_leases_locked(now_mono)
            owned = [
                key for key, lease in self._leases.items()
                if key[0] == dataset and lease.holder_id == holder_id
            ]
            if not owned:
                raise RegistryNotFoundError(
                    f"No live leases for holder '{holder_id}' in dataset '{dataset}'"
                )
            for key in owned:
                self._leases[key].expires_at = new_mono
        return new_wall

    # -----------------------------------------------------------------------
    # Internal — output generation
    # -----------------------------------------------------------------------

    def _get_store(self, dataset: str) -> RegistryStore:
        if dataset not in self._stores:
            self._stores[dataset] = RegistryStore(self._database_dir / dataset)
        return self._stores[dataset]

    def _regenerate_output(self, dataset: str) -> bool:
        """Rebuild output cache, write service.json if content changed. Returns True if changed."""
        with self._lock:
            entries = self._entries.get(dataset, {})
            output = [self._entry_to_output(e) for e in entries.values()]
            old_output = self._output_cache.get(dataset)
            changed = output != old_output
            self._output_cache[dataset] = output

        if changed:
            self._get_store(dataset).write_service_json(output)
            if self._on_service_changed:
                self._on_service_changed(dataset)
        return changed

    def _entry_to_output(self, entry: RegistryEntry) -> dict:
        """Convert a RegistryEntry to service.json output format.

        Output schema: {id, type, name, description, metadata}
          - description: system-generated; used by taxonomy build (LLM text input)
          - metadata:    for A2A = full agent card; for generic = {url?, inputSchema?}
        """
        if entry.type == "skill" and entry.skill_data:
            sd = entry.skill_data
            return {
                "id": entry.service_id,
                "type": "skill",
                "name": sd.name,
                "description": sd.description,
                "metadata": {
                    "skill_path": sd.skill_path,
                    "license": sd.license,
                    "files": sd.files,
                },
            }

        if entry.type == "generic" and entry.service_data:
            sd = entry.service_data
            metadata: dict = {}
            if sd.inputSchema:
                metadata["inputSchema"] = sd.inputSchema
            if sd.url:
                metadata["url"] = sd.url
            return {
                "id": entry.service_id,
                "type": "generic",
                "name": sd.name,
                "description": sd.description,
                "metadata": metadata,
            }

        if entry.type == "a2a" and entry.agent_card:
            card = entry.agent_card
            return {
                "id": entry.service_id,
                "type": "a2a",
                "name": card.name,
                "description": build_description(card),   # agent desc + skills (for taxonomy build)
                "metadata": card.model_dump(exclude_none=True),
            }

        # a2a with unresolved card (URL fetch failed)
        return {
            "id": entry.service_id,
            "type": "a2a",
            "name": entry.service_id,
            "description": f"Unresolved agent card: {entry.agent_card_url or 'unknown'}",
            "metadata": {},
        }

    # -----------------------------------------------------------------------
    # Internal — taxonomy state
    # -----------------------------------------------------------------------

    def _init_taxonomy_state(self, dataset: str) -> TaxonomyState:
        """Compute initial taxonomy state on startup and cache it."""
        state = self._compute_taxonomy_state(dataset)
        with self._lock:
            self._taxonomy_states[dataset] = state
        return state

    def _compute_taxonomy_state(self, dataset: str) -> TaxonomyState:
        """Compare current service hash against build_config.json's stored hash."""
        build_config_path = self._database_dir / dataset / "taxonomy" / BUILD_CONFIG_FILE
        taxonomy_path     = self._database_dir / dataset / "taxonomy" / TAXONOMY_FILE

        if not build_config_path.exists() or not taxonomy_path.exists():
            return TaxonomyState.NONEXISTENT

        stored_hash = _read_build_hash(build_config_path)
        if stored_hash is None:
            return TaxonomyState.NONEXISTENT

        with self._lock:
            current_services = self._output_cache.get(dataset, [])
        current_hash = _compute_build_hash(current_services)

        return TaxonomyState.AVAILABLE if current_hash == stored_hash else TaxonomyState.UNAVAILABLE

    def _mark_taxonomy_stale(self, dataset: str):
        """Mark taxonomy STALE after a CRUD operation (only if currently AVAILABLE)."""
        with self._lock:
            if self._taxonomy_states.get(dataset) == TaxonomyState.AVAILABLE:
                self._taxonomy_states[dataset] = TaxonomyState.STALE

    # -----------------------------------------------------------------------
    # Dataset lifecycle
    # -----------------------------------------------------------------------

    def create_dataset(
        self,
        name: str,
        embedding_model: Optional[str] = None,
        formats: Optional[Dict[str, Any]] = None,
    ) -> Path:
        """Create a new empty dataset directory with vector + register configs.

        Args:
            name: Dataset folder name under ``database/``.
            embedding_model: Vector embedding model key. ``None`` resolves
                to ``vector.utils.embedding.DEFAULT_EMBEDDING_MODEL``.
            formats: Per-type min_version map. Defaults to all three types
                at ``v0.0``. Unknown types / versions are silently dropped.

        Returns the dataset directory path.
        Raises ValueError if the dataset already exists, or if a non-default
        ``formats`` normalizes to an empty dict (which would reject every
        registration).
        """
        ds_dir = self._database_dir / name
        if ds_dir.exists():
            raise ValueError(f"Dataset '{name}' already exists")
        if embedding_model is None:
            from a2x_registry.vector.utils.embedding import DEFAULT_EMBEDDING_MODEL
            embedding_model = DEFAULT_EMBEDDING_MODEL
        # Validate formats first so we fail fast before touching disk.
        normalized = self._normalize_or_default_formats(formats)
        self._init_dataset_files(name, embedding_model, normalized)
        logger.info("Created dataset '%s' (embedding: %s, formats: %s)",
                    name, embedding_model, normalized)
        return ds_dir

    def _normalize_or_default_formats(
        self, formats: Optional[Dict[str, Any]],
    ) -> Dict[str, str]:
        """Resolve a caller-supplied ``formats`` arg to a usable map.

        - ``None`` → defaults (all three types at v0.0)
        - dict → normalized via ``normalize_format_config``; raises if the
          result is empty (would reject every registration)
        """
        if formats is None:
            return dict(DEFAULT_FORMAT_CONFIG)
        normalized = normalize_format_config(formats)
        if not normalized:
            raise ValueError(
                "formats must declare at least one valid type/version. "
                f"Supported types: {list(SUPPORTED_SERVICE_TYPES)}")
        return normalized

    def _init_dataset_files(
        self,
        name: str,
        embedding_model: str,
        formats: Dict[str, str],
    ) -> None:
        """Idempotent: create the dataset dir + write both config files.

        Single source of truth for "what files make a dataset valid"
        (used by both ``create_dataset`` happy-path and
        ``_ensure_dataset_initialized``'s reconcile fallback). Never
        clobbers either config file if it's already present, so:

        - manually-set embedding models survive an auto-init (e.g. user
          ran ``POST /vector-config`` before the first register)
        - manually-restricted formats survive an auto-init (e.g. user
          ran ``POST /register-config`` before the first register)
        """
        ds_dir = self._database_dir / name
        ds_dir.mkdir(parents=True, exist_ok=True)
        (ds_dir / "query").mkdir(exist_ok=True)
        if not (ds_dir / "vector_config.json").exists():
            self.set_vector_config(name, embedding_model)
        if not (ds_dir / "register_config.json").exists():
            self.set_register_config(name, formats)

    def delete_dataset(self, name: str) -> None:
        """Delete a dataset directory and all internal caches.

        Raises ValueError if the dataset directory does not exist.
        """
        import shutil
        ds_dir = self._database_dir / name
        if not ds_dir.exists():
            raise ValueError(f"Dataset '{name}' does not exist")
        with self._lock:
            self._entries.pop(name, None)
            self._output_cache.pop(name, None)
            self._taxonomy_states.pop(name, None)
            self._stores.pop(name, None)
            self._format_configs.pop(name, None)
        shutil.rmtree(ds_dir)
        logger.info("Deleted dataset '%s'", name)

    # -----------------------------------------------------------------------
    # Internal — unified format validation
    # -----------------------------------------------------------------------

    def get_register_config(self, dataset: str) -> Dict[str, str]:
        """Return the effective ``{type: min_version}`` map for a dataset.

        Resolution order:
          1. cached in-memory copy (if already read this session)
          2. ``<dataset>/register_config.json`` (normalized; unknown types dropped)
          3. default — all three types allowed from ``v0.0``
        Missing file → returns defaults AND caches them (does not write).
        """
        with self._lock:
            cached = self._format_configs.get(dataset)
        if cached is not None:
            return dict(cached)
        cfg = self._get_store(dataset).load_register_config()
        if cfg is None:
            cfg = dict(DEFAULT_FORMAT_CONFIG)
        with self._lock:
            self._format_configs[dataset] = dict(cfg)
        return dict(cfg)

    def get_vector_config(self, dataset: str) -> Dict[str, Any]:
        """Return ``{embedding_model, embedding_dim}`` for a dataset.

        If ``vector_config.json`` is missing, returns the system default
        without writing it (callers can use this to render a "current
        effective" view without forcing a write).
        """
        from a2x_registry.vector.utils.embedding import DEFAULT_EMBEDDING_MODEL, EMBEDDING_MODELS
        cfg = self._get_store(dataset).load_vector_config()
        if cfg is not None:
            return dict(cfg)
        return {
            "embedding_model": DEFAULT_EMBEDDING_MODEL,
            "embedding_dim": EMBEDDING_MODELS[DEFAULT_EMBEDDING_MODEL]["dim"],
        }

    def set_vector_config(
        self,
        dataset: str,
        embedding_model: Optional[str] = None,
        embedding_dim: Optional[int] = None,
    ) -> Dict[str, Any]:
        """Persist a new embedding config for a dataset.

        Resolves ``embedding_dim`` from the ``EMBEDDING_MODELS`` table when
        only ``embedding_model`` is provided; raises ``ValueError`` if the
        model is unknown and no explicit dim was given.

        The caller is responsible for triggering any vector-index rebuild
        side effects (e.g. ``SearchService.schedule_vector_sync``).
        """
        from a2x_registry.vector.utils.embedding import DEFAULT_EMBEDDING_MODEL, EMBEDDING_MODELS
        model_name = embedding_model or DEFAULT_EMBEDDING_MODEL
        info = EMBEDDING_MODELS.get(model_name)
        dim = info["dim"] if info else embedding_dim
        if dim is None:
            raise ValueError(
                f"Unknown embedding model '{model_name}'; provide embedding_dim explicitly"
            )
        self._get_store(dataset).write_vector_config(model_name, dim)
        return {"embedding_model": model_name, "embedding_dim": dim}

    def set_register_config(self, dataset: str, formats: Dict[str, Any]) -> Dict[str, str]:
        """Persist a new ``formats`` mapping for a dataset.

        Unknown types / versions are silently dropped (see ``normalize_format_config``).
        If the resulting map is empty, raises ``ValueError`` — a dataset with
        no allowed formats would reject every registration.
        """
        cfg = normalize_format_config(formats)
        if not cfg:
            raise ValueError(
                "formats must declare at least one valid type with a known version; "
                f"supported types: {list(SUPPORTED_SERVICE_TYPES)}")
        self._get_store(dataset).write_register_config(cfg)
        with self._lock:
            self._format_configs[dataset] = dict(cfg)
        return dict(cfg)

    def _require_type_allowed(self, dataset: str, service_type: str) -> str:
        """Ensure ``service_type`` is allowed by the dataset config; return its min_version."""
        cfg = self.get_register_config(dataset)
        if service_type not in cfg:
            raise ValueError(
                f"Service type '{service_type}' is not allowed for dataset '{dataset}'. "
                f"Allowed: {sorted(cfg.keys())}")
        return cfg[service_type]

    def _require_valid(self, dataset: str, service_type: str, payload: Any) -> ValidationResult:
        """Validate or raise ValueError. Returns the passing ValidationResult."""
        min_version = self._require_type_allowed(dataset, service_type)
        # Legacy override: fixed a2a allow-list from constructor wins for a2a.
        if service_type == "a2a" and self._allowed_a2a_versions is not None:
            result = validate_agent_card(payload, self._allowed_a2a_versions)
        else:
            result = validate_service(service_type, payload, min_version)
        if not result.valid:
            raise ValueError(
                f"{service_type} payload failed validation for dataset '{dataset}': "
                + "; ".join(result.errors))
        if result.warnings:
            logger.info("%s payload passed as %s (%s) with warnings: %s",
                        service_type, result.matched_version, dataset,
                        "; ".join(result.warnings))
        return result

    def _validate_entry(self, dataset: str, entry: RegistryEntry,
                        skip_a2a_url_only: bool = False) -> Optional[ValidationResult]:
        """Startup-side validation. Returns None when validation is skipped
        (e.g. an A2A entry whose card has not yet been fetched).
        """
        cfg = self.get_register_config(dataset)
        if entry.type not in cfg:
            return ValidationResult(
                valid=False, service_type=entry.type,
                errors=[f"service type '{entry.type}' not allowed in dataset '{dataset}'"])
        min_version = cfg[entry.type]

        if entry.type == "a2a":
            if entry.agent_card is None:
                # URL-only entries are validated after the fetch phase.
                return None if skip_a2a_url_only else ValidationResult(
                    valid=False, service_type="a2a",
                    errors=["agent_card not present (URL fetch not yet completed)"])
            if self._allowed_a2a_versions is not None:
                return validate_agent_card(entry.agent_card, self._allowed_a2a_versions)
            return validate_service("a2a", entry.agent_card, min_version)

        if entry.type == "generic" and entry.service_data:
            payload = {
                "name": entry.service_data.name,
                "description": entry.service_data.description,
            }
            return validate_service("generic", payload, min_version)

        if entry.type == "skill" and entry.skill_data:
            payload = {
                "name": entry.skill_data.name,
                "description": entry.skill_data.description,
            }
            return validate_service("skill", payload, min_version)

        return ValidationResult(
            valid=False, service_type=entry.type,
            errors=["entry has no payload to validate"])

    def _fetch_agent_cards_parallel(self, url_entries: List[tuple]):
        with ThreadPoolExecutor(max_workers=10) as executor:
            futures = {
                executor.submit(fetch_agent_card, entry.agent_card_url): (dataset, entry)
                for dataset, entry in url_entries
            }
            for future in as_completed(futures):
                dataset, entry = futures[future]
                try:
                    card = future.result()
                    with self._lock:
                        entry.agent_card = card
                    logger.info("Fetched agent card '%s' from %s", entry.service_id, entry.agent_card_url)
                except Exception as e:
                    if entry.agent_card:
                        logger.warning("Failed to fetch %s, using cached snapshot: %s", entry.agent_card_url, e)
                    else:
                        logger.warning("Failed to fetch %s, no cache: %s", entry.agent_card_url, e)

    # -----------------------------------------------------------------------
    # Internal — dataset discovery / global config
    # -----------------------------------------------------------------------

    def _discover_datasets(self) -> List[str]:
        if not self._database_dir.exists():
            return []
        return sorted(
            d.name for d in self._database_dir.iterdir()
            if d.is_dir() and (
                (d / USER_CONFIG_FILE).exists()
                or (d / API_CONFIG_FILE).exists()
                or (d / "register_config.json").exists()
                or (d / "skills").is_dir()
            )
        )

    def _distribute_global_config(self):
        try:
            with open(self._global_config_path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError) as e:
            logger.warning("Failed to load global config %s: %s", self._global_config_path, e)
            return

        by_dataset: Dict[str, list] = {}
        for svc in data.get("services", []):
            ds = svc.pop("dataset", "default")
            by_dataset.setdefault(ds, []).append(svc)

        for dataset, services in by_dataset.items():
            dataset_dir = self._database_dir / dataset
            dataset_dir.mkdir(parents=True, exist_ok=True)
            user_config_path = dataset_dir / USER_CONFIG_FILE
            if not user_config_path.exists():
                content = json.dumps({"services": services}, ensure_ascii=False, indent=2)
                with open(user_config_path, "w", encoding="utf-8") as f:
                    f.write(content)
                logger.info("Created %s from global config (%d services)", user_config_path, len(services))


# ---------------------------------------------------------------------------
# Module-level utilities
# ---------------------------------------------------------------------------

def _compute_build_hash(services: List[dict]) -> str:
    """Hash name+description pairs only, order-independent.

    Only these two fields are used by taxonomy build (LLM classification),
    so only they should trigger a rebuild when changed.
    """
    pairs = sorted((s["name"], s.get("description", "")) for s in services)
    return hashlib.sha256(json.dumps(pairs, ensure_ascii=False).encode()).hexdigest()


def _read_build_hash(build_config_path: Path) -> Optional[str]:
    """Read service_hash from build_config.json, or None if absent/invalid."""
    try:
        with open(build_config_path, "r", encoding="utf-8") as f:
            return json.load(f).get("service_hash")
    except (json.JSONDecodeError, OSError):
        return None
