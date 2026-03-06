"""Unit tests for extended protected resource metadata."""

import pytest
from pydantic import ValidationError

from mcp_auth_ext.metadata import AuthSchemeDescriptor, ExtendedProtectedResourceMetadata


def test_auth_scheme_descriptor_minimal() -> None:
    """scheme_id only is valid; params is None."""
    d = AuthSchemeDescriptor(scheme_id="oauth2")
    assert d.scheme_id == "oauth2"
    assert d.params is None


def test_auth_scheme_descriptor_with_params() -> None:
    """Descriptor with params provides client info (e.g. header_name, param_name)."""
    d = AuthSchemeDescriptor(
        scheme_id="api_key",
        params={"header_name": "X-API-Key"},
    )
    assert d.params is not None
    assert d.params["header_name"] == "X-API-Key"


def test_extended_prm_parse_rfc9728_only() -> None:
    """Extended PRM parses standard RFC 9728 JSON; authorization_schemes is None."""
    json = """
    {
        "resource": "https://example.com/mcp",
        "authorization_servers": ["https://auth.example.com"]
    }
    """
    prm = ExtendedProtectedResourceMetadata.model_validate_json(json)
    assert str(prm.resource) == "https://example.com/mcp"
    assert len(prm.authorization_servers) == 1
    assert prm.authorization_schemes is None


def test_extended_prm_parse_with_authorization_schemes() -> None:
    """Extended PRM parses JSON with authorization_schemes (scheme_id + params)."""
    json = """
    {
        "resource": "https://example.com/mcp",
        "authorization_servers": ["https://auth.example.com"],
        "authorization_schemes": [
            {"scheme_id": "oauth2"},
            {"scheme_id": "api_key", "params": {"header_name": "X-API-Key"}}
        ]
    }
    """
    prm = ExtendedProtectedResourceMetadata.model_validate_json(json)
    assert prm.authorization_schemes is not None
    assert len(prm.authorization_schemes) == 2
    assert prm.authorization_schemes[0].scheme_id == "oauth2"
    assert prm.authorization_schemes[1].params == {"header_name": "X-API-Key"}


def test_extended_prm_serialize_roundtrip() -> None:
    """Extended PRM serializes to JSON that parses back (RFC 9728 + schemes)."""
    prm = ExtendedProtectedResourceMetadata(
        resource="https://example.com/mcp",
        authorization_servers=["https://auth.example.com"],
        authorization_schemes=[
            AuthSchemeDescriptor(scheme_id="oauth2"),
            AuthSchemeDescriptor(scheme_id="api_key", params={"header_name": "X-API-Key"}),
        ],
    )
    data = prm.model_dump(mode="json")
    assert "authorization_schemes" in data
    prm2 = ExtendedProtectedResourceMetadata.model_validate(data)
    assert prm2.authorization_schemes is not None
    assert len(prm2.authorization_schemes) == 2


def test_extended_prm_parse_empty_authorization_schemes() -> None:
    """Extended PRM accepts empty authorization_schemes list (distinct from omit/None)."""
    json = """
    {
        "resource": "https://example.com/mcp",
        "authorization_servers": ["https://auth.example.com"],
        "authorization_schemes": []
    }
    """
    prm = ExtendedProtectedResourceMetadata.model_validate_json(json)
    assert prm.authorization_schemes is not None
    assert len(prm.authorization_schemes) == 0


def test_extended_prm_invalid_missing_authorization_servers() -> None:
    """Invalid: missing authorization_servers fails."""
    with pytest.raises(ValidationError):
        ExtendedProtectedResourceMetadata.model_validate_json(
            '{"resource": "https://example.com/mcp"}'
        )
