"""Synthetic admin AuthContext used by the heartbeat sweeper.

The sweeper hard-deletes services via ``RegistryService.deregister(ds, sid,
caller=SYSTEM_CTX)``. We use a real ``AuthContext`` with ``role="admin"``
to bypass the owner check — same code path as an admin user invoking
``DELETE /services/{sid}``. ``principal_id="_system"`` shows up in audit
log so operators can distinguish sweeper-driven deletes from manual ones.
"""

from __future__ import annotations

from a2x_registry.common.auth_context import AuthContext


SYSTEM_CTX: AuthContext = AuthContext(
    principal_id="_system",
    role="admin",
    namespaces=None,  # global; admin invariant
)
