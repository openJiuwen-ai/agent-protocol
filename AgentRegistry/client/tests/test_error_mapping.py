"""Transport must map HTTP 401 → A2XAuthenticationError, 403 → A2XAuthorizationError."""

from __future__ import annotations

import httpx
import pytest

from a2x_registry_client.errors import (
    A2XAuthenticationError,
    A2XAuthorizationError,
    A2XHTTPError,
    NotFoundError,
    ValidationError,
)
from a2x_registry_client.transport import HTTPTransport


def _transport_with_handler(handler):
    """Build an HTTPTransport whose underlying httpx.Client routes through a mock."""
    mock = httpx.MockTransport(handler)
    t = HTTPTransport(base_url="http://test/")
    # Replace the internal client with one using the mock transport.
    t._client.close()
    t._client = httpx.Client(base_url="http://test/", transport=mock)
    return t


def test_401_maps_to_auth_error():
    def handler(_req):
        return httpx.Response(401, json={"detail": "Missing token"})

    with pytest.raises(A2XAuthenticationError) as exc_info:
        _transport_with_handler(handler).request("GET", "/anything")
    assert exc_info.value.status_code == 401
    # Hierarchy: also an A2XHTTPError so generic except still catches.
    assert isinstance(exc_info.value, A2XHTTPError)


def test_403_maps_to_authorization_error():
    def handler(_req):
        return httpx.Response(403, json={"detail": "Forbidden"})

    with pytest.raises(A2XAuthorizationError) as exc_info:
        _transport_with_handler(handler).request("GET", "/anything")
    assert exc_info.value.status_code == 403
    assert isinstance(exc_info.value, A2XHTTPError)


def test_404_unchanged():
    def handler(_req):
        return httpx.Response(404, json={"detail": "Missing"})

    with pytest.raises(NotFoundError):
        _transport_with_handler(handler).request("GET", "/x")


def test_400_unchanged():
    def handler(_req):
        return httpx.Response(400, json={"detail": "Bad shape"})

    with pytest.raises(ValidationError):
        _transport_with_handler(handler).request("POST", "/x")


def test_2xx_returns_response():
    def handler(_req):
        return httpx.Response(200, json={"ok": True})

    resp = _transport_with_handler(handler).request("GET", "/x")
    assert resp.status_code == 200
    assert resp.json() == {"ok": True}


def test_auth_error_classes_distinct():
    """An ``except A2XAuthenticationError`` must NOT catch a 403, and vice versa."""
    assert not issubclass(A2XAuthenticationError, A2XAuthorizationError)
    assert not issubclass(A2XAuthorizationError, A2XAuthenticationError)
