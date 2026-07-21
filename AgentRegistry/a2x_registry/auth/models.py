"""Persistent models for the auth store: Principal and ApiKey.

These mirror the schemas written to ``auth_data/principals.json`` and
``auth_data/api_keys.json``. Pydantic-validated so malformed JSON on disk
is rejected at load time, not silently absorbed.
"""

from __future__ import annotations

from typing import List, Optional

from pydantic import BaseModel, Field, field_validator

from a2x_registry.common.auth_context import AuthContext

# Role enum — kept as Literal-string for JSON round-trip simplicity.
Role = str  # one of: "admin" | "provider" | "user"

VALID_ROLES = ("admin", "provider", "user")


class Principal(BaseModel):
    """A registered identity that may hold one or more API keys.

    ``namespaces`` semantics:
      - ``None`` → all namespaces (only legal for ``role="admin"``).
      - ``list[str]`` (possibly empty) → exactly these dataset names.
        Empty list is legal — used to "soft-disable" a principal without
        revoking their keys; every namespace-scoped check will 403.
    """

    id: str
    handle: str
    role: Role
    namespaces: Optional[List[str]] = None
    created_at: str       # ISO 8601 UTC, e.g. "2026-04-28T10:00:00Z"
    disabled_at: Optional[str] = None
    note: str = ""

    @field_validator("role")
    @classmethod
    def _check_role(cls, v: str) -> str:
        if v not in VALID_ROLES:
            raise ValueError(f"role must be one of {VALID_ROLES}, got {v!r}")
        return v

    @field_validator("namespaces")
    @classmethod
    def _check_namespaces(cls, v, info):
        # role isn't always available during validation order; cross-field
        # consistency is enforced by AuthStore.create_principal instead.
        return v

    def to_context(self) -> AuthContext:
        """Convert into the neutral ``AuthContext`` shape consumed by RegistryService.

        admin → ``namespaces=None`` propagates ("all"); provider/user → frozen
        set of their list (may be empty, signalling "no access anywhere").
        """
        ns = None if self.namespaces is None else frozenset(self.namespaces)
        return AuthContext(principal_id=self.id, role=self.role, namespaces=ns)

    @property
    def is_disabled(self) -> bool:
        return self.disabled_at is not None


class ApiKey(BaseModel):
    """A single API key bound to a Principal.

    Only ``key_hash`` is sufficient to authenticate. ``key_prefix`` is for
    display / log redaction. Plaintext is never persisted — it lives in
    the create-response body and the bootstrap stderr banner, nowhere else.
    """

    key_id: str             # "k_<8-12 hex>" — opaque, listable
    principal_id: str
    key_hash: str           # hex sha256
    key_prefix: str         # "a2x_pat_xxxx" — 12 chars
    name: str = ""          # human-given label, e.g. "alice-laptop"
    created_at: str
    # Optional expiry. Set forward-compat-only in Phase 1; no enforcement
    # code yet. Loader still serializes it back unchanged so we don't need
    # a follow-up migration when expiry checks are added.
    expires_at: Optional[str] = None
    last_used_at: Optional[str] = None
    revoked_at: Optional[str] = None

    @property
    def is_revoked(self) -> bool:
        return self.revoked_at is not None
