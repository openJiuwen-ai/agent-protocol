"""FastAPI dependencies + module-level AuthStore singleton.

The store is held as a module-global so the FastAPI dependency layer can
short-circuit cheaply when it's None (= "registry not auth-initialized,
behave as anonymous everywhere"). Initialization is done by
``backend.startup`` at server boot time via ``set_auth_store(...)``.
"""

from __future__ import annotations

from typing import Optional

from fastapi import Depends, Header, HTTPException, Request

from a2x_registry.common.auth_context import AuthContext

from .errors import AuthenticationError
from .store import AuthStore

# ── Module-level singleton (set by backend.startup) ──────────────────────

_store: Optional[AuthStore] = None


def get_auth_store() -> Optional[AuthStore]:
    """Return the active AuthStore, or ``None`` if auth is not initialized."""
    return _store


def set_auth_store(store: Optional[AuthStore]) -> None:
    """Inject (or clear) the global AuthStore. Called from backend startup
    + from test fixtures. Tests must reset to ``None`` between cases that
    toggle auth-on / auth-off.
    """
    global _store
    _store = store


# ── Dependencies ─────────────────────────────────────────────────────────


def _parse_bearer(authorization: Optional[str]) -> Optional[str]:
    """Return the token from a ``Bearer <token>`` header, else None.

    Tolerant: case-insensitive scheme; trims whitespace. Returns None when
    the header is absent or doesn't look like bearer — distinguishes "you
    didn't authenticate" from "you tried but failed", which the dep uses
    to choose between 401 and silent anon.
    """
    if not authorization:
        return None
    parts = authorization.strip().split(None, 1)
    if len(parts) != 2 or parts[0].lower() != "bearer":
        return None
    return parts[1].strip() or None


async def authorize(
    request: Request,
    authorization: Optional[str] = Header(default=None),
) -> Optional[AuthContext]:
    """Universal per-route auth dependency.

    Behavior — three branches:

    1. **Anon fast-path**: the auth store hasn't been initialized OR the
       request's ``{dataset}`` path-param resolves to an anonymous
       namespace (``is_auth_required(ds) == False``). Returns ``None``.
       Pre-existing test suite + every legacy SDK call lands here.

    2. **Authenticated**: token is present and valid; the principal's
       ``namespaces`` includes the dataset (admin short-circuits this
       check). Returns the ``AuthContext``.

    3. **Refused**: namespace requires auth but the token is missing /
       invalid (raise 401) or out-of-scope (raise 403). Always audited.

    Routes that have NO ``{dataset}`` in their path (e.g.
    ``/api/auth/principals`` or ``POST /api/datasets`` itself) get a
    ``None`` from branch 1 and use the layered ``require_admin`` dep
    below for their own checks.
    """
    store = get_auth_store()
    dataset = request.path_params.get("dataset")

    # Branch 1a: registry has no auth at all → fully anonymous.
    if store is None:
        return None

    # Branch 1b: route has a {dataset} param and that dataset is anonymous.
    # We need to consult the registry to know — lazy import to avoid a
    # circular dep from auth/ ← register/ ← auth/.
    if dataset is not None:
        from a2x_registry.backend.routers.dataset import get_registry_service
        try:
            svc = get_registry_service()
        except Exception:
            svc = None
        if svc is not None and not svc.is_auth_required(dataset):
            return None

    # Branch 2 / 3: this request must carry a valid token.
    token = _parse_bearer(authorization)
    if token is None:
        # Distinguish "anon read on auth-required namespace" — return None
        # rather than raise. Read-only endpoints opt in via ``optional_principal``;
        # mutating endpoints layer ``require_principal`` on top and will 401.
        # NOTE: for the "auth-required" branch we want a hard 401 even on reads,
        # because the design says auth-required namespaces are not publicly
        # readable. The router can override this by using ``optional_principal``
        # explicitly if it wants laxer reads later.
        store.audit("auth.failed", reason="no_token", dataset=dataset)
        raise HTTPException(
            status_code=401,
            detail="Authentication required for this namespace",
            headers={"WWW-Authenticate": "Bearer"},
        )

    try:
        ctx = store.authenticate(token)
    except AuthenticationError as exc:
        raise HTTPException(
            status_code=401,
            detail=str(exc),
            headers={"WWW-Authenticate": "Bearer"},
        ) from exc

    # Namespace check (admin short-circuits)
    if dataset is not None and not ctx.is_admin:
        if ctx.namespaces is None or dataset not in ctx.namespaces:
            store.audit(
                "permission.denied",
                reason="namespace_out_of_scope",
                principal_id=ctx.principal_id,
                dataset=dataset,
            )
            raise HTTPException(
                status_code=403,
                detail=f"Principal lacks access to namespace {dataset!r}",
            )

    return ctx


async def require_principal(
    ctx: Optional[AuthContext] = Depends(authorize),
) -> AuthContext:
    """Same as ``authorize`` but rejects the anon branch with 401.

    Use on endpoints that must always identify the caller — e.g.
    ``/api/auth/whoami`` and ``/api/auth/keys/*`` — independent of any
    namespace-level toggle.
    """
    if ctx is None:
        raise HTTPException(
            status_code=401,
            detail="Authentication required",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return ctx


async def require_admin(
    ctx: AuthContext = Depends(require_principal),
) -> AuthContext:
    """Reject non-admin callers with 403.

    Used by every admin-only endpoint (``POST /api/auth/principals`` etc.).
    NB: this dep chains through ``authorize`` so per-namespace gating
    still applies first — fine for the /api/auth/* router where there's
    no dataset in the path. For endpoints whose path DOES include a
    dataset but the operation is meta-level (e.g. toggling auth-config
    on an anon dataset), use ``require_admin_strict`` below which skips
    the namespace gate.
    """
    if not ctx.is_admin:
        store = get_auth_store()
        if store is not None:
            store.audit(
                "permission.denied", reason="admin_required",
                principal_id=ctx.principal_id, role=ctx.role,
            )
        raise HTTPException(status_code=403, detail="Admin role required")
    return ctx


async def require_admin_strict(
    authorization: Optional[str] = Header(default=None),
) -> AuthContext:
    """Admin authentication that BYPASSES per-namespace gating.

    Used for meta-operations on a specific dataset where the namespace
    auth flag isn't yet what we care about — e.g. flipping ``auth_config``
    on an anonymous dataset (the namespace-aware ``authorize`` would
    short-circuit to None and never reach the admin check).

    Pre-bootstrap (store is None) → 404; same surface as /api/auth/*.
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
    token = _parse_bearer(authorization)
    if token is None:
        raise HTTPException(
            status_code=401,
            detail="Admin token required",
            headers={"WWW-Authenticate": "Bearer"},
        )
    try:
        ctx = store.authenticate(token)
    except AuthenticationError as exc:
        raise HTTPException(
            status_code=401, detail=str(exc),
            headers={"WWW-Authenticate": "Bearer"},
        ) from exc
    if not ctx.is_admin:
        store.audit(
            "permission.denied", reason="admin_required",
            principal_id=ctx.principal_id, role=ctx.role,
        )
        raise HTTPException(status_code=403, detail="Admin role required")
    return ctx


async def require_admin_or_anon(
    ctx: Optional[AuthContext] = Depends(authorize),
) -> Optional[AuthContext]:
    """Gate for dataset-level operations under mixed-mode registries.

    Three-way semantics:
      - ``ctx is None`` (anon namespace / store not initialized) → allow
        through. The legacy fully-anonymous behavior is preserved on
        anon namespaces.
      - ``ctx.is_admin`` → allow through. Admin can manage every dataset.
      - else (provider / user) → 403. Even if they have namespace access
        for per-entry mutations, dataset-level operations (delete dataset,
        rewrite register-config, vector-config, auth-config) need admin.
    """
    if ctx is None or ctx.is_admin:
        return ctx
    store = get_auth_store()
    if store is not None:
        store.audit(
            "permission.denied", reason="admin_required_for_dataset_ops",
            principal_id=ctx.principal_id, role=ctx.role,
        )
    raise HTTPException(
        status_code=403,
        detail="Admin role required for dataset-level operation",
    )
