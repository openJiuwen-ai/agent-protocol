"""Static API Key authentication module for A2X Registry.

Per-namespace opt-in: a registry remains fully anonymous until
``a2x-registry auth init`` runs, and individual namespaces stay anonymous
unless they were created with ``auth_required=true``. See
``reference/分析报告/auth_design_sequence.md`` for the full design
(path contains non-ASCII characters and is retained as-is per the
upstream reference tree layout).

Public API:
    - ``AuthStore`` — file-backed credential store
    - ``Role`` — string enum: ``admin`` / ``provider`` / ``user``
    - ``Principal`` / ``ApiKey`` — Pydantic models
    - ``AuthenticationError`` / ``AuthorizationError`` — domain exceptions
    - ``get_auth_store`` / ``set_auth_store`` — module-level singleton hooks
    - ``router`` — FastAPI router for ``/api/auth/*``

The store / models / tokens layer is FastAPI-free (testable without uvicorn).
Web concerns live in ``deps.py`` (Depends helpers) and ``router.py``.
"""

from .errors import AuthenticationError, AuthorizationError
from .models import ApiKey, Principal, Role
from .store import AuthStore
from .deps import get_auth_store, set_auth_store
from .tokens import TOKEN_PREFIX, generate_token, hash_token, token_prefix

__all__ = [
    "AuthStore",
    "Role",
    "Principal",
    "ApiKey",
    "AuthenticationError",
    "AuthorizationError",
    "get_auth_store",
    "set_auth_store",
    "TOKEN_PREFIX",
    "generate_token",
    "hash_token",
    "token_prefix",
]
