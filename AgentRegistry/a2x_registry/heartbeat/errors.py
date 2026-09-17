"""Domain exceptions for the heartbeat module.

All inherit from ``ValueError`` so the existing ``_run`` helper in
``backend/routers/dataset.py`` auto-maps them to HTTP 400. The router can
then optionally upgrade the response body to a structured shape (with
``code`` + bound info) by catching these subclasses explicitly before the
generic ``ValueError`` branch.
"""

from __future__ import annotations

from typing import Optional


class HeartbeatError(ValueError):
    """Base class — all heartbeat-domain rejections inherit from this.

    Carries an optional ``code`` (machine-readable) and ``min_ttl`` /
    ``max_ttl`` bounds so the router can render structured 400 bodies.
    """

    code: str = "heartbeat_error"

    def __init__(
        self,
        message: str,
        *,
        min_ttl: Optional[int] = None,
        max_ttl: Optional[int] = None,
    ) -> None:
        super().__init__(message)
        self.min_ttl = min_ttl
        self.max_ttl = max_ttl


class HeartbeatNotSupportedError(HeartbeatError):
    """Client sent ``lease_ttl`` on a namespace whose ``lease_config`` is disabled.

    The 4-corner matrix's "namespace ❌ / client ✅" cell. Returns 400 with
    ``code=heartbeat_not_supported`` so SDK can surface "this namespace
    doesn't accept heartbeats; remove lease_ttl from your request."
    """

    code = "heartbeat_not_supported"


class TTLRequiredError(HeartbeatError):
    """Namespace requires heartbeats but client didn't supply ``lease_ttl``.

    The 4-corner matrix's "namespace ✅ / client ❌" cell. Response body
    includes the namespace's ``min_ttl`` / ``max_ttl`` so the client knows
    the acceptable range without a second round-trip.
    """

    code = "ttl_required"


class TTLOutOfRangeError(HeartbeatError):
    """Client's ``lease_ttl`` violates the namespace's ``[min_ttl, max_ttl]``.

    The 4-corner matrix's "namespace ✅ / client ✅ out of range" cell.
    Response body includes the actual bounds so the client SDK can decide
    whether to auto-retry with a clamped value or surface to the user.
    """

    code = "ttl_out_of_range"
