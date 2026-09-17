"""Neutral authorization context, dependency-free.

This module is imported by ``a2x_registry.register`` and the FastAPI router
layer so that ``RegistryService`` mutation methods can accept caller identity
without taking a dependency on the ``a2x_registry.auth`` package. The auth
module builds ``AuthContext`` instances; the register module only consumes
the read-only fields below for owner / namespace / role checks.

Keeping this dataclass in ``common/`` makes the directionality explicit:
``register/ ──depends on──> common/`` and ``auth/ ──depends on──> common/``,
but ``register/`` never imports ``auth/``.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import FrozenSet, Optional


@dataclass(frozen=True)
class AuthContext:
    """Opaque caller identity for service-layer authorization checks.

    Attributes:
        principal_id: Stable opaque id from the auth store. This is the value
            written to ``RegistryEntry.owner_id`` and ``_Lease.holder_id``.
        role: One of ``"admin"``, ``"provider"``, ``"user"``. The service
            layer only inspects this for the admin short-circuit; full role
            policy lives in the FastAPI dependency.
        namespaces: Frozen set of dataset names this principal may act in.
            ``None`` means "all namespaces" — only legal for ``role=="admin"``.
            The service layer trusts this; the FastAPI dependency validates
            namespace membership before constructing the context.
    """

    principal_id: str
    role: str
    namespaces: Optional[FrozenSet[str]] = None

    @property
    def is_admin(self) -> bool:
        return self.role == "admin"
