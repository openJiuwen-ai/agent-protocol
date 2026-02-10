"""Tests for server-side extended PRM helpers."""

import pytest
from pydantic import AnyHttpUrl
from starlette.applications import Starlette
from starlette.testclient import TestClient

from mcp_auth_ext.metadata import AuthSchemeDescriptor, ExtendedProtectedResourceMetadata
from mcp_auth_ext.server import build_extended_prm_payload, create_extended_protected_resource_routes


def test_build_extended_prm_payload_minimal() -> None:
    """Build payload with required args only."""
    payload = build_extended_prm_payload(
        resource_url="https://example.com/mcp",
        authorization_servers=["https://auth.example.com"],
    )
    assert isinstance(payload, ExtendedProtectedResourceMetadata)
    assert str(payload.resource) == "https://example.com/mcp"
    assert len(payload.authorization_servers) == 1
    assert payload.authorization_schemes is None


def test_build_extended_prm_payload_with_schemes_dict() -> None:
    """Build payload with authorization_schemes as dicts (scheme_id + params)."""
    payload = build_extended_prm_payload(
        resource_url="https://example.com/mcp",
        authorization_servers=["https://auth.example.com"],
        authorization_schemes=[
            {"scheme_id": "oauth2"},
            {"scheme_id": "api_key", "params": {"header_name": "X-API-Key"}},
        ],
    )
    assert payload.authorization_schemes is not None
    assert len(payload.authorization_schemes) == 2
    assert payload.authorization_schemes[1].params == {"header_name": "X-API-Key"}


def test_build_extended_prm_payload_with_schemes_descriptors() -> None:
    """Build payload with authorization_schemes as AuthSchemeDescriptor."""
    payload = build_extended_prm_payload(
        resource_url="https://example.com/mcp",
        authorization_servers=["https://auth.example.com"],
        authorization_schemes=[
            AuthSchemeDescriptor(scheme_id="api_key", params={"header_name": "X-API-Key"}),
        ],
    )
    assert payload.authorization_schemes is not None
    assert payload.authorization_schemes[0].params == {"header_name": "X-API-Key"}


def test_build_extended_prm_payload_serializable() -> None:
    """Payload is JSON-serializable and valid RFC 9728 + extension."""
    payload = build_extended_prm_payload(
        resource_url="https://example.com/mcp",
        authorization_servers=["https://auth.example.com"],
        authorization_schemes=[{"scheme_id": "oauth2"}],
    )
    data = payload.model_dump(mode="json")
    assert "resource" in data
    assert "authorization_servers" in data
    assert "authorization_schemes" in data
    assert data["authorization_schemes"][0]["scheme_id"] == "oauth2"


def test_build_extended_prm_payload_with_optional_rfc9728_fields() -> None:
    """Build payload with scopes_supported, resource_name, resource_documentation."""
    payload = build_extended_prm_payload(
        resource_url="https://example.com/mcp",
        authorization_servers=["https://auth.example.com"],
        scopes_supported=["mcp:read"],
        resource_name="My MCP Server",
        resource_documentation="https://example.com/docs",
    )
    assert payload.scopes_supported == ["mcp:read"]
    assert payload.resource_name == "My MCP Server"
    assert payload.resource_documentation is not None
    assert str(payload.resource_documentation) == "https://example.com/docs"


def test_create_extended_protected_resource_routes_returns_list() -> None:
    """create_extended_protected_resource_routes returns one route at well-known path."""
    metadata = ExtendedProtectedResourceMetadata(
        resource=AnyHttpUrl("https://example.com/mcp"),
        authorization_servers=[AnyHttpUrl("https://auth.example.com")],
    )
    routes = create_extended_protected_resource_routes(metadata)
    assert len(routes) == 1
    assert "/.well-known/oauth-protected-resource/mcp" in routes[0].path


def test_create_extended_protected_resource_routes_serves_metadata_on_get() -> None:
    """GET on the well-known path returns PRM JSON with Cache-Control."""
    metadata = ExtendedProtectedResourceMetadata(
        resource=AnyHttpUrl("https://example.com/mcp"),
        authorization_servers=[AnyHttpUrl("https://auth.example.com")],
        authorization_schemes=[
            AuthSchemeDescriptor(scheme_id="oauth2", params={"scope": "mcp:read"}),
        ],
    )
    routes = create_extended_protected_resource_routes(metadata)
    app = Starlette(routes=routes)
    client = TestClient(app)
    response = client.get(routes[0].path)
    assert response.status_code == 200
    data = response.json()
    assert data["resource"] == "https://example.com/mcp"
    assert "https://auth.example.com" in data["authorization_servers"][0]
    assert len(data["authorization_servers"]) == 1
    assert len(data["authorization_schemes"]) == 1
    assert data["authorization_schemes"][0]["scheme_id"] == "oauth2"
    assert data["authorization_schemes"][0]["params"] == {"scope": "mcp:read"}
    assert response.headers.get("cache-control") == "public, max-age=3600"
