"""Per-namespace authorization for the cluster handshake.

Reuses the existing auth module — no new auth logic. When an instance
receives an OPEN, it decides, per requested namespace, whether to sync it,
following the same semantics as ``auth/deps`` (``store is None`` = no auth
= allow everything):

  - namespace the receiver DOESN'T have → needs an ``admin`` token (so it
    can host an ephemeral copy); allowed outright when the receiver has no
    auth configured. Accepted namespaces of this kind are "ephemeral".
  - namespace the receiver HAS → allowed when it isn't ``auth_required``;
    otherwise needs a ``provider`` (or admin) token scoped to it.
"""

from __future__ import annotations

import logging
from typing import List, Optional, Tuple

logger = logging.getLogger(__name__)


def authorize_namespaces(
    registry_svc,
    auth_store,
    requested: List[str],
    token: Optional[str],
) -> Tuple[List[str], List[str]]:
    """Return ``(accepted, ephemeral)`` namespace name lists.

    ``accepted`` ⊇ ``ephemeral``; ephemeral are the accepted namespaces the
    receiver doesn't have locally (it will host a memory-only copy for the
    session and drop it on disconnect).
    """
    accepted: List[str] = []
    ephemeral: List[str] = []

    # Resolve the caller's auth context once (None when no auth / bad token).
    ctx = None
    if auth_store is not None and token:
        try:
            ctx = auth_store.authenticate(token)
        except Exception:  # noqa: BLE001 — any auth failure → unauthenticated
            ctx = None

    existing = set(registry_svc.list_datasets()) if registry_svc is not None else set()

    for ns in requested:
        if ns in existing:
            # Receiver already hosts this namespace.
            if auth_store is None or not registry_svc.is_auth_required(ns):
                accepted.append(ns)
            elif ctx is not None and (
                ctx.is_admin
                or (ctx.namespaces is not None and ns in ctx.namespaces)
            ):
                accepted.append(ns)
            # else: rejected (no provider access)
        else:
            # Receiver must create an ephemeral copy → admin-gated.
            if auth_store is None:
                accepted.append(ns)
                ephemeral.append(ns)
            elif ctx is not None and ctx.is_admin:
                accepted.append(ns)
                ephemeral.append(ns)
            # else: rejected (no admin to create namespace)

    return accepted, ephemeral
