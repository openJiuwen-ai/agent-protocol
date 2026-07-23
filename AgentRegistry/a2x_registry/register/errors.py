"""Registration-domain exceptions.

These exceptions live in the **business layer** (``RegistryService``) and
intentionally carry no HTTP semantics. The FastAPI router translates them
into HTTP status codes (e.g. ``RegistryNotFoundError`` → ``404``); the
client SDK in turn translates the HTTP status back into its own Python
exception (``NotFoundError``).
"""


class RegistryNotFoundError(Exception):
    """Raised when a requested registry resource (dataset / service / skill)
    does not exist. Distinct from ``KeyError`` so the router can map it
    unambiguously to HTTP 404 without catching unrelated dict misses.
    """


class NotFoundError(Exception):
    """Raised when a requested resource (registry or row) does not exist.

    Used by the SQL-backed generic CRUD (RegistryTableService) when a named
    registry or a service_id is absent. The router maps this to HTTP 404.
    """


class ValidationError(Exception):
    """Raised when input fails validation (unknown kind, missing service_id,
    unknown column in patch/filter). The router maps this to HTTP 400.
    """


class NotOwnedError(Exception):
    """Raised when a caller attempts to mutate a resource they do not own.
    The router maps this to HTTP 403.
    """


class ImageInUseError(Exception):
    """Raised when deregistering an image that still has running instances.
    The router maps this to HTTP 409 (conflict).
    """


class ExternalDependencyError(Exception):
    """Raised when an external dependency (image repository, etc.) fails.
    The router maps this to HTTP 502.
    """
