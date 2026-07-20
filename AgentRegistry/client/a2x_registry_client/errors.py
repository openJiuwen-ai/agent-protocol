"""Exception hierarchy for the A2X Registry client SDK.

All errors raised by the SDK inherit from ``A2XError``. HTTP-origin errors
further inherit from ``A2XHTTPError`` and carry ``status_code`` / ``payload``;
local-only errors (e.g. ownership violations) do not.
"""

from __future__ import annotations

from typing import Any


class A2XError(Exception):
    """Base class for all SDK errors."""

    def __init__(
        self,
        message: str,
        *,
        status_code: int | None = None,
        payload: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.status_code = status_code
        self.payload = payload


class A2XConnectionError(A2XError):
    """Network / timeout failures (httpx.ConnectError, TimeoutException, ...)."""


class A2XHTTPError(A2XError):
    """Any 4xx/5xx response from the backend."""


class NotFoundError(A2XHTTPError):
    """404 — resource not found."""


class ValidationError(A2XHTTPError):
    """400 / 422 — request rejected by the backend."""


class A2XAuthenticationError(A2XHTTPError):
    """401 — missing, invalid, or revoked API key.

    The server returned ``401 Unauthorized``. The accompanying message
    usually distinguishes between "no token sent", "wrong prefix",
    "hash miss", "revoked", and "principal disabled". Recovering means
    fixing or replacing the credential; the same call won't succeed by
    retrying.
    """


class A2XAuthorizationError(A2XHTTPError):
    """403 — authenticated but the principal lacks the required permission.

    Reasons include: role too low (e.g. ``user`` trying to mutate),
    namespace not in the principal's ``namespaces`` list, owner mismatch
    on a per-entry mutation, or holder mismatch on lease release.
    Different from 401: the token itself is valid; the operation just
    isn't allowed.
    """


class UserConfigServiceImmutableError(ValidationError):
    """Update or delete refused because the service originates from ``user_config``.

    The backend rejects both ``PUT`` and ``DELETE`` on services whose ``source``
    is ``user_config`` — callers must edit ``user_config.json`` directly.
    """


class A2XHeartbeatNotSupportedError(ValidationError):
    """Client passed ``lease_ttl`` but the target namespace doesn't enable heartbeat.

    The server's 4-corner matrix's "namespace ❌ / client ✅" cell. Inherits
    from ``ValidationError`` so legacy ``except ValidationError`` paths
    still catch it; new code can ``except A2XHeartbeatNotSupportedError``
    for precise handling.
    """


class A2XTTLRequiredError(ValidationError):
    """Namespace requires a heartbeat lease but the client didn't supply ``lease_ttl``.

    Carries the namespace's ``min_ttl`` / ``max_ttl`` bounds (parsed from
    the 400 response body) so the SDK can auto-retry with a sensible
    default without a second round-trip.
    """

    def __init__(self, message: str, *, status_code=None, payload=None) -> None:
        super().__init__(message, status_code=status_code, payload=payload)
        body = payload or {}
        self.min_ttl: int | None = body.get("min_ttl")
        self.max_ttl: int | None = body.get("max_ttl")


class A2XTTLOutOfRangeError(ValidationError):
    """Client's ``lease_ttl`` doesn't satisfy the namespace's ``[min_ttl, max_ttl]``.

    Carries the bounds for SDK / user to react. Common SDK strategy:
    auto-retry once with ``min(max(client_ttl, min_ttl), max_ttl)`` if
    ``auto_clamp=True`` is set on the client; otherwise surface to caller.
    """

    def __init__(self, message: str, *, status_code=None, payload=None) -> None:
        super().__init__(message, status_code=status_code, payload=payload)
        body = payload or {}
        self.min_ttl: int | None = body.get("min_ttl")
        self.max_ttl: int | None = body.get("max_ttl")


# Backward-compatible alias (old name was deregister-specific, but the same
# backend rejection covers update_agent too). Will be removed in a future major.
UserConfigDeregisterForbiddenError = UserConfigServiceImmutableError


class UnexpectedServiceTypeError(A2XHTTPError):
    """``get_agent`` received a non-JSON payload (e.g. a skill ZIP)."""


class ServerError(A2XHTTPError):
    """5xx — backend internal error."""


class NotOwnedError(A2XError):
    """Local ownership check failed; no HTTP request was sent."""

    def __init__(self, dataset: str, service_id: str) -> None:
        super().__init__(
            f"service {service_id!r} in dataset {dataset!r} was not registered by this client"
        )
        self.dataset = dataset
        self.service_id = service_id
