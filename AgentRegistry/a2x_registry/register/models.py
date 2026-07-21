"""Data models for the registration module."""

from enum import Enum
from typing import List, Literal, Optional

from pydantic import BaseModel, ConfigDict


class TaxonomyState(str, Enum):
    """State of the A2X taxonomy for a dataset.

    NONEXISTENT — no taxonomy built yet (or build_config.json missing)
    AVAILABLE   — taxonomy hash matches current service.json → safe to use
    UNAVAILABLE — hash mismatch: service.json changed since last build → search blocked
    STALE       — CRUD happened after last hash check; will be re-evaluated before next search
    """
    NONEXISTENT = "nonexistent"
    AVAILABLE   = "available"
    UNAVAILABLE = "unavailable"
    STALE       = "stale"


# --- Generic service ---

class GenericServiceData(BaseModel):
    name: str
    description: str
    inputSchema: dict = {}
    url: Optional[str] = None


class SkillData(BaseModel):
    name: str
    description: str
    license: str = ""
    skill_path: str = ""       # relative path: "skills/{name}"
    files: List[str] = []      # relative file paths in folder


# --- A2A Agent Card (aligned with A2A protocol spec) ---

class AgentSkill(BaseModel):
    id: str = ""
    name: str
    description: str = ""
    tags: List[str] = []
    examples: List[str] = []
    inputModes: List[str] = []
    outputModes: List[str] = []


class AgentProvider(BaseModel):
    organization: str = ""
    url: str = ""


class AgentCapabilities(BaseModel):
    streaming: bool = False
    pushNotifications: bool = False
    stateTransitionHistory: bool = False


class AgentCard(BaseModel):
    model_config = ConfigDict(extra="allow")

    name: str
    description: str
    version: str = ""
    protocolVersion: str = ""
    url: str = ""
    preferredTransport: str = ""
    provider: Optional[AgentProvider | dict] = None
    capabilities: Optional[AgentCapabilities | dict | list] = None
    skills: List[AgentSkill] = []
    defaultInputModes: List[str] = []
    defaultOutputModes: List[str] = []
    documentationUrl: str = ""
    iconUrl: str = ""


# --- Registry entry (internal unified representation) ---

class RegistryEntry(BaseModel):
    service_id: str
    type: Literal["generic", "a2a", "skill"]
    source: Literal["user_config", "api_config", "ephemeral", "skill_folder"]
    service_data: Optional[GenericServiceData] = None
    agent_card: Optional[AgentCard] = None
    agent_card_url: Optional[str] = None
    skill_data: Optional[SkillData] = None
    # Auth-aware ownership. ``None`` for legacy entries, entries from
    # ``user_config.json``, and any entry registered against an
    # ``auth_required=false`` namespace. Set to the caller principal_id when
    # an authenticated registration lands on an auth-required namespace.
    # Entries with ``owner_id=None`` in an auth-required namespace are
    # "unclaimed" and only admin can mutate them.
    owner_id: Optional[str] = None
    # Heartbeat lease TTL (seconds) the client requested at registration.
    # ``None`` → permanent service (legacy / namespace doesn't enable
    # heartbeat / client didn't request one). Presence signals "this entry
    # expects periodic heartbeats; sweeper will mark it unhealthy if absent".
    # Persisted so the registry knows the renewal TTL after restart; the
    # actual ``expires_at`` countdown stays in HeartbeatStore in-memory.
    lease_ttl: Optional[int] = None


# --- HTTP request models ---

class RegisterGenericRequest(BaseModel):
    service_id: Optional[str] = None
    dataset: str = "default"
    name: str
    description: str
    inputSchema: dict = {}
    url: str = ""
    persistent: bool = True
    # Optional heartbeat lease request. When present, the namespace must have
    # ``lease_config.enabled=true`` AND the value must satisfy ``[min_ttl,
    # max_ttl]`` from that config — otherwise 400. When absent, the service
    # is permanent (legacy behavior). See docs/heartbeat_design.md.
    lease_ttl: Optional[int] = None


class RegisterA2ARequest(BaseModel):
    service_id: Optional[str] = None
    dataset: str = "default"
    agent_card: Optional[AgentCard] = None
    agent_card_url: Optional[str] = None
    persistent: bool = True
    lease_ttl: Optional[int] = None   # same semantics as RegisterGenericRequest


# --- HTTP response models ---

class RegisterResponse(BaseModel):
    service_id: str
    dataset: str
    status: str  # "registered" | "updated"
    # Populated when registration was granted a heartbeat lease. Absent
    # (None) for permanent services so the JSON shape stays byte-equal to
    # pre-heartbeat output (Pydantic's exclude_none on the model_dump).
    lease_ttl: Optional[int] = None         # echoed from request, post-validation
    lease_expires_at: Optional[float] = None  # unix wall-clock (for client display)


class DeregisterResponse(BaseModel):
    service_id: str
    status: str  # "deregistered" — missing service raises RegistryNotFoundError → 404


class UpdateResponse(BaseModel):
    service_id: str
    dataset: str
    status: str                       # "updated" — missing service raises RegistryNotFoundError → 404
    changed_fields: List[str] = []    # top-level keys whose value actually changed
    taxonomy_affected: bool = False   # True iff name or description changed


class SkillResponse(BaseModel):
    name: str
    dataset: str
    service_id: str = ""
    status: str  # "registered" | "updated" | "deleted" | "not_found"


class RegistryStatus(BaseModel):
    total_services: int
    by_source: dict  # {"user_config": N, "api_config": N, "ephemeral": N}
    datasets: List[str]


class BuildRequest(BaseModel):
    resume: str = "no"  # "no" | "yes" | "keyword"
    # Tree structure
    generic_ratio: Optional[float] = None
    delete_threshold: Optional[int] = None
    max_service_size: Optional[int] = None
    max_categories_size: Optional[int] = None
    max_depth: Optional[int] = None
    min_leaf_size: Optional[int] = None
    # Keyword extraction
    keyword_batch_size: Optional[int] = None
    max_keywords_per_service: Optional[int] = None
    keyword_threshold: Optional[int] = None
    # Classification iteration
    classification_retries: Optional[int] = None
    max_refine_iterations: Optional[int] = None
    # Temperature
    temperature_keywords: Optional[float] = None
    temperature_design: Optional[float] = None
    temperature_classify: Optional[float] = None
    # Token limits
    max_tokens_design: Optional[int] = None
    max_tokens_design_small: Optional[int] = None
    max_tokens_classify: Optional[int] = None
    max_tokens_validate: Optional[int] = None
    max_tokens_keywords: Optional[int] = None
    # Cross-domain & workers
    enable_cross_domain: Optional[bool] = None
    workers: Optional[int] = None
    # Logging
    log_level: Optional[str] = None  # "DEBUG" | "INFO" | "WARNING" | "ERROR"
