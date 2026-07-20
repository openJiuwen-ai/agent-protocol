"""Registration-domain exceptions.

These exceptions live in the **business layer** (``RegistryService``) and
intentionally carry no HTTP semantics. The FastAPI router translates them
into HTTP status codes (e.g. ``RegistryNotFoundError`` → ``404``); the
client SDK in turn translates the HTTP status back into its own Python
exception (``NotFoundError``). See ``docs/client_design.md`` §3.4.
"""


class RegistryNotFoundError(Exception):
    """Raised when a requested registry resource (dataset / service / skill)
    does not exist. Distinct from ``KeyError`` so the router can map it
    unambiguously to HTTP 404 without catching unrelated dict misses.
    """
