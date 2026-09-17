"""AuthStore — file-backed credential store.

Persisted layout (under ``~/.a2x_registry/auth_data/`` by default;
``<A2X_REGISTRY_HOME>/auth_data`` when the env var is set;
``$A2X_REGISTRY_AUTH_DATA`` for a per-resource explicit override):

    principals.json   — list[Principal] dumps
    api_keys.json     — list[ApiKey] dumps
    audit.log         — JSONL, append-only

All writes go through ``_atomic_write`` (tmp + os.replace) so a crash
mid-write can't leave a partial JSON file. Reads happen once at load
time and again on any subsequent ``write_*`` (re-derive in-memory
indices). The hot path — ``authenticate(token)`` — is a single sha256
hex lookup against the in-memory ``_keys_by_hash`` dict.

The store is intentionally NOT a singleton class — that's
``deps.get_auth_store()``'s job. This module only defines the type and
its instantiation rules (``load_or_none`` / ``bootstrap``).
"""

from __future__ import annotations

import json
import logging
import os
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from a2x_registry.common.auth_context import AuthContext

from .errors import AuthenticationError, AuthorizationError
from .models import ApiKey, Principal, VALID_ROLES
from .tokens import (
    constant_time_equals,
    generate_token,
    hash_token,
    token_prefix,
    TOKEN_PREFIX,
)

logger = logging.getLogger(__name__)

PRINCIPALS_FILE = "principals.json"
KEYS_FILE = "api_keys.json"
AUDIT_LOG_FILE = "audit.log"


def default_data_dir() -> Path:
    """Resolve the auth_data directory.

    Order:
      1. ``$A2X_REGISTRY_AUTH_DATA`` (per-resource explicit override)
      2. ``<A2X_REGISTRY_HOME>/auth_data`` (when the env var is set)
      3. ``./auth_data`` under CWD if CWD contains ``database/`` (source-tree
         dev mode parity with ``database_dir()``)
      4. ``~/.a2x_registry/auth_data`` (default user home)

    Mirrors the ``database/`` and ``llm_apikey.json`` resolution shape so
    a fresh ``pip install`` + ``a2x-registry auth init`` writes outside
    ``site-packages`` and survives venv re-creation / package upgrade.
    """
    env = os.environ.get("A2X_REGISTRY_AUTH_DATA", "").strip()
    if env:
        return Path(env)
    from a2x_registry.common.paths import get_home
    return get_home() / "auth_data"


def _utcnow_iso() -> str:
    """ISO-8601 UTC string, second precision. Matches existing log style."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _atomic_write(path: Path, data: Any) -> None:
    """Write JSON atomically to ``path`` via tmp + os.replace.

    Mirrors ``register/store._atomic_write`` shape (same crash-safety
    posture) but kept local to avoid coupling auth to register internals.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


class AuthStore:
    """File-backed credential store + in-memory hot-path indices.

    Instances are constructed via the class methods ``load_or_none`` /
    ``bootstrap`` — never ``__init__`` directly from outside the module.
    """

    def __init__(self, data_dir: Path):
        self._dir = data_dir
        self._lock = threading.Lock()
        self._principals: Dict[str, Principal] = {}
        self._keys: Dict[str, ApiKey] = {}            # key_id → ApiKey
        self._keys_by_hash: Dict[str, str] = {}       # key_hash → key_id

    # ── construction ────────────────────────────────────────────────────

    @classmethod
    def load_or_none(cls, data_dir: Optional[Path] = None) -> Optional["AuthStore"]:
        """Return a loaded store if bootstrap has happened, else ``None``.

        Detection: ``principals.json`` exists. If it does, read both files;
        if it doesn't, the registry hasn't been auth-initialized yet — the
        FastAPI dependency layer interprets a ``None`` store as "fully
        anonymous, legacy behavior everywhere".
        """
        data_dir = data_dir or default_data_dir()
        if not (data_dir / PRINCIPALS_FILE).exists():
            return None
        store = cls(data_dir)
        store._load_from_disk()
        return store

    @classmethod
    def bootstrap(
        cls,
        data_dir: Optional[Path] = None,
        admin_token: Optional[str] = None,
        admin_handle: str = "root",
    ) -> Tuple["AuthStore", str]:
        """Initialize a fresh auth store with one admin principal + key.

        Idempotent-refusal: if ``principals.json`` already exists, raises
        ``FileExistsError`` — callers should route to ``a2x-registry auth
        reset-admin --confirm`` to rotate instead.

        Returns ``(store, plaintext_admin_token)``. The token is the only
        moment the plaintext exists outside the network/CLI surface; the
        caller MUST surface it to the operator (print to stderr / banner).
        """
        data_dir = data_dir or default_data_dir()
        if (data_dir / PRINCIPALS_FILE).exists():
            raise FileExistsError(
                f"Auth already initialized at {data_dir}. "
                f"Run 'a2x-registry auth reset-admin --confirm' to rotate the admin key."
            )
        data_dir.mkdir(parents=True, exist_ok=True)
        store = cls(data_dir)
        token = admin_token or generate_token()
        if not token.startswith(TOKEN_PREFIX):
            raise ValueError(f"admin_token must start with {TOKEN_PREFIX!r}")
        # Use distinct id prefixes so principal ids and key ids are
        # textually distinguishable in logs and audit entries.
        principal = Principal(
            id=cls._new_id("u"),
            handle=admin_handle,
            role="admin",
            namespaces=None,
            created_at=_utcnow_iso(),
            disabled_at=None,
            note="Bootstrap admin (root)",
        )
        key = ApiKey(
            key_id=cls._new_id("k"),
            principal_id=principal.id,
            key_hash=hash_token(token),
            key_prefix=token_prefix(token),
            name="bootstrap",
            created_at=_utcnow_iso(),
        )
        store._principals[principal.id] = principal
        store._keys[key.key_id] = key
        store._keys_by_hash[key.key_hash] = key.key_id
        store._persist_principals()
        store._persist_keys()
        store._audit(
            "principal.created",
            principal_id=principal.id,
            role="admin",
            by="bootstrap",
        )
        store._audit(
            "key.created",
            key_id=key.key_id,
            principal_id=principal.id,
            key_prefix=key.key_prefix,
            by="bootstrap",
        )
        return store, token

    # ── load / persist ──────────────────────────────────────────────────

    def _load_from_disk(self) -> None:
        path_p = self._dir / PRINCIPALS_FILE
        path_k = self._dir / KEYS_FILE
        with self._lock:
            self._principals = {}
            self._keys = {}
            self._keys_by_hash = {}
            try:
                with open(path_p, "r", encoding="utf-8") as f:
                    raw = json.load(f)
                for item in raw:
                    p = Principal(**item)
                    self._principals[p.id] = p
            except (OSError, json.JSONDecodeError) as exc:
                logger.error("Failed to load %s: %s", path_p, exc)
                raise
            if path_k.exists():
                try:
                    with open(path_k, "r", encoding="utf-8") as f:
                        raw = json.load(f)
                    for item in raw:
                        k = ApiKey(**item)
                        self._keys[k.key_id] = k
                        self._keys_by_hash[k.key_hash] = k.key_id
                except (OSError, json.JSONDecodeError) as exc:
                    logger.error("Failed to load %s: %s", path_k, exc)
                    raise

    def _persist_principals(self) -> None:
        data = [p.model_dump() for p in self._principals.values()]
        _atomic_write(self._dir / PRINCIPALS_FILE, data)

    def _persist_keys(self) -> None:
        data = [k.model_dump() for k in self._keys.values()]
        _atomic_write(self._dir / KEYS_FILE, data)

    # ── audit log ───────────────────────────────────────────────────────

    def _audit(self, event: str, **fields: Any) -> None:
        """Append a JSONL audit record. Never raises (best-effort)."""
        entry = {"ts": _utcnow_iso(), "event": event, **fields}
        line = json.dumps(entry, ensure_ascii=False)
        try:
            self._dir.mkdir(parents=True, exist_ok=True)
            with open(self._dir / AUDIT_LOG_FILE, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError as exc:
            logger.warning("Audit log write failed (%s): %s", exc, line)

    def audit(self, event: str, **fields: Any) -> None:
        """Public alias for audit recording (used by router on permission denials)."""
        self._audit(event, **fields)

    # ── authentication hot path ─────────────────────────────────────────

    def authenticate(self, token: str) -> AuthContext:
        """Verify a plaintext bearer token and return its AuthContext.

        Raises ``AuthenticationError`` on every failure mode (no token,
        wrong prefix, hash miss, revoked, expired, disabled principal).
        Failure modes are logged to the audit log with reason categories
        so operators can distinguish brute-force probes from honest typos.
        """
        if not token or not isinstance(token, str):
            self._audit("auth.failed", reason="empty_token")
            raise AuthenticationError("Empty or non-string token")
        if not token.startswith(TOKEN_PREFIX):
            self._audit(
                "auth.failed", reason="wrong_prefix",
                key_prefix=token[:12] if len(token) >= 12 else token,
            )
            raise AuthenticationError("Token has wrong prefix")
        h = hash_token(token)
        with self._lock:
            key_id = self._keys_by_hash.get(h)
        if key_id is None:
            self._audit(
                "auth.failed", reason="invalid_token",
                key_prefix=token_prefix(token),
            )
            raise AuthenticationError("Invalid API key")
        # Re-fetch under lock to ensure we read consistent state
        with self._lock:
            key = self._keys.get(key_id)
            principal = (
                self._principals.get(key.principal_id) if key is not None else None
            )
        if key is None or principal is None:
            self._audit(
                "auth.failed", reason="dangling_key", key_id=key_id,
                key_prefix=token_prefix(token),
            )
            raise AuthenticationError("Key references a missing principal")
        if not constant_time_equals(key.key_hash, h):
            # Should never trip given the dict lookup, but defense-in-depth.
            self._audit("auth.failed", reason="hash_mismatch", key_id=key.key_id)
            raise AuthenticationError("Hash mismatch")
        if key.is_revoked:
            self._audit("auth.failed", reason="revoked", key_id=key.key_id)
            raise AuthenticationError("API key has been revoked")
        # expires_at present-but-not-enforced in Phase 1 — schema-only.
        if principal.is_disabled:
            self._audit(
                "auth.failed", reason="principal_disabled",
                principal_id=principal.id, key_id=key.key_id,
            )
            raise AuthenticationError(f"Principal '{principal.handle}' is disabled")
        # Touch last_used_at (best-effort; no fsync) — useful for "long
        # unused → recommend revoke" audits later.
        with self._lock:
            key.last_used_at = _utcnow_iso()
        return principal.to_context()

    # ── principal CRUD ──────────────────────────────────────────────────

    def create_principal(
        self,
        handle: str,
        role: str,
        namespaces: Optional[List[str]],
        note: str = "",
        by: Optional[str] = None,
    ) -> Tuple[Principal, str]:
        """Create a new principal + return its first API key plaintext.

        Validation:
          - role must be one of ``admin`` / ``provider`` / ``user``
          - admin: ``namespaces`` must be ``None``; provider/user: must be
            a non-None list (empty list is legal but renders the principal
            useless — kept on purpose so admins can stage an account)
          - handle uniqueness is enforced — same-handle creates → ValueError

        Returns the persisted ``Principal`` and the plaintext token (the
        ONLY moment plaintext exists; caller surfaces it via the create
        response body / banner).
        """
        if role not in VALID_ROLES:
            raise ValueError(f"role must be one of {VALID_ROLES}, got {role!r}")
        if role == "admin" and namespaces is not None:
            raise ValueError("admin principal must have namespaces=None (all)")
        if role != "admin" and not isinstance(namespaces, list):
            raise ValueError(f"{role} principal requires namespaces=list (got {type(namespaces).__name__})")
        with self._lock:
            for p in self._principals.values():
                if p.handle == handle:
                    raise ValueError(f"handle {handle!r} already in use")
            principal = Principal(
                id=self._new_id("u"),
                handle=handle,
                role=role,
                namespaces=namespaces,
                created_at=_utcnow_iso(),
                disabled_at=None,
                note=note,
            )
            self._principals[principal.id] = principal
            token = generate_token()
            key = ApiKey(
                key_id=self._new_id("k"),
                principal_id=principal.id,
                key_hash=hash_token(token),
                key_prefix=token_prefix(token),
                name="initial",
                created_at=_utcnow_iso(),
            )
            self._keys[key.key_id] = key
            self._keys_by_hash[key.key_hash] = key.key_id
            self._persist_principals()
            self._persist_keys()
        self._audit(
            "principal.created",
            principal_id=principal.id, role=role, by=by,
        )
        self._audit(
            "key.created",
            key_id=key.key_id, principal_id=principal.id,
            key_prefix=key.key_prefix, by=by,
        )
        return principal, token

    def list_principals(self) -> List[Principal]:
        with self._lock:
            return list(self._principals.values())

    def get_principal(self, principal_id: str) -> Optional[Principal]:
        with self._lock:
            return self._principals.get(principal_id)

    def update_principal(
        self,
        principal_id: str,
        *,
        namespaces: Optional[List[str]] = "__UNSET__",  # sentinel
        role: Optional[str] = None,
        disabled: Optional[bool] = None,
        note: Optional[str] = None,
        by: Optional[str] = None,
    ) -> Principal:
        """Partial update of a Principal's mutable fields.

        ``namespaces=None`` is meaningful (means "all" for admin), so we use
        a sentinel string to distinguish "not provided" from "set to None".
        Role transitions are allowed but the namespaces invariant must hold
        after the change (admin↔None, others↔list).
        """
        with self._lock:
            principal = self._principals.get(principal_id)
            if principal is None:
                raise KeyError(f"Principal {principal_id!r} not found")
            new_role = role or principal.role
            new_ns = principal.namespaces if namespaces == "__UNSET__" else namespaces
            if new_role not in VALID_ROLES:
                raise ValueError(f"role must be one of {VALID_ROLES}, got {new_role!r}")
            if new_role == "admin" and new_ns is not None:
                raise ValueError("admin principal must have namespaces=None (all)")
            if new_role != "admin" and not isinstance(new_ns, list):
                raise ValueError(f"{new_role} principal requires namespaces=list")
            new_note = principal.note if note is None else note
            new_disabled_at = principal.disabled_at
            if disabled is True:
                new_disabled_at = new_disabled_at or _utcnow_iso()
            elif disabled is False:
                new_disabled_at = None
            updated = Principal(
                id=principal.id,
                handle=principal.handle,
                role=new_role,
                namespaces=new_ns,
                created_at=principal.created_at,
                disabled_at=new_disabled_at,
                note=new_note,
            )
            self._principals[principal_id] = updated
            self._persist_principals()
        self._audit("principal.updated", principal_id=principal_id, by=by)
        return updated

    # ── key CRUD ────────────────────────────────────────────────────────

    def create_key(
        self,
        principal_id: str,
        name: str = "",
        by: Optional[str] = None,
    ) -> Tuple[ApiKey, str]:
        """Issue a new API key for an existing principal. Returns (key, plaintext)."""
        with self._lock:
            if principal_id not in self._principals:
                raise KeyError(f"Principal {principal_id!r} not found")
            token = generate_token()
            key = ApiKey(
                key_id=self._new_id("k"),
                principal_id=principal_id,
                key_hash=hash_token(token),
                key_prefix=token_prefix(token),
                name=name,
                created_at=_utcnow_iso(),
            )
            self._keys[key.key_id] = key
            self._keys_by_hash[key.key_hash] = key.key_id
            self._persist_keys()
        self._audit(
            "key.created", key_id=key.key_id, principal_id=principal_id,
            key_prefix=key.key_prefix, by=by,
        )
        return key, token

    def list_keys(self, principal_id: Optional[str] = None) -> List[ApiKey]:
        """List keys, optionally filtered to one principal. Plaintext NEVER returned."""
        with self._lock:
            keys = list(self._keys.values())
        if principal_id is not None:
            keys = [k for k in keys if k.principal_id == principal_id]
        return keys

    def revoke_key(self, key_id: str, by: Optional[str] = None) -> ApiKey:
        """Mark a key revoked. Idempotent: re-revoke returns the existing record unchanged."""
        with self._lock:
            key = self._keys.get(key_id)
            if key is None:
                raise KeyError(f"Key {key_id!r} not found")
            if not key.is_revoked:
                key.revoked_at = _utcnow_iso()
                # Drop from the by-hash index so authenticate() misses fast.
                self._keys_by_hash.pop(key.key_hash, None)
                self._persist_keys()
        self._audit(
            "key.revoked", key_id=key_id, principal_id=key.principal_id, by=by,
        )
        return key

    # ── helpers ─────────────────────────────────────────────────────────

    @staticmethod
    def _new_id(prefix: str) -> str:
        """Generate a short opaque id with a prefix (``u_`` or ``k_``).

        12 hex chars after the underscore = 48 bits of entropy. With <1000
        principals/keys per registry the birthday-collision probability is
        negligible; we don't need full UUIDs.
        """
        import secrets as _s
        return f"{prefix}_{_s.token_hex(6)}"

    @property
    def data_dir(self) -> Path:
        return self._dir
