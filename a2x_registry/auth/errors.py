"""Domain exceptions for the auth module.

These are plain Python exceptions; the FastAPI dependency layer
(``deps.py``) maps them to HTTP 401 / 403. Keeping them framework-free
lets ``AuthStore`` be unit-tested without uvicorn / TestClient.
"""

from __future__ import annotations


class AuthenticationError(Exception):
    """Caller failed to prove identity — translates to HTTP 401.

    Reasons: no ``Authorization`` header; wrong scheme; token prefix mismatch;
    sha256 lookup miss; token revoked; token expired; principal disabled.
    """


class AuthorizationError(Exception):
    """Caller is identified but lacks permission — translates to HTTP 403.

    Reasons: role insufficient (e.g. user trying to mutate); namespace not in
    the principal's ``namespaces`` list; service owner mismatch.
    """
