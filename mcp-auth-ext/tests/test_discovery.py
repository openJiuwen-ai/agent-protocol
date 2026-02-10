"""Tests for extended PRM discovery."""

import pytest
import respx
import httpx
from httpx import AsyncClient

from mcp_auth_ext import fetch_extended_protected_resource_metadata
from mcp_auth_ext.metadata import ExtendedProtectedResourceMetadata


@respx.mock
@pytest.mark.asyncio
async def test_fetch_extended_prm_success_root_url() -> None:
    """Discovery uses root well-known when server_url has no path."""
    prm_json = """
    {
        "resource": "https://example.com/",
        "authorization_servers": ["https://auth.example.com"]
    }
    """
    respx.get("https://example.com/.well-known/oauth-protected-resource").mock(
        return_value=httpx.Response(200, content=prm_json.encode())
    )
    result = await fetch_extended_protected_resource_metadata(
        "https://example.com",
        http_client=AsyncClient(),
    )
    assert result is not None
    assert isinstance(result, ExtendedProtectedResourceMetadata)
    assert str(result.resource) == "https://example.com/"


@respx.mock
@pytest.mark.asyncio
async def test_fetch_extended_prm_success_with_path() -> None:
    """Discovery tries path-based well-known when server_url has path."""
    prm_json = """
    {
        "resource": "https://example.com/mcp",
        "authorization_servers": ["https://auth.example.com"],
        "authorization_schemes": [{"scheme_id": "oauth2"}]
    }
    """
    respx.get("https://example.com/.well-known/oauth-protected-resource/mcp").mock(
        return_value=httpx.Response(200, content=prm_json.encode())
    )
    result = await fetch_extended_protected_resource_metadata(
        "https://example.com/mcp",
        http_client=AsyncClient(),
    )
    assert result is not None
    assert result.authorization_schemes is not None
    assert len(result.authorization_schemes) == 1
    assert result.authorization_schemes[0].scheme_id == "oauth2"


@respx.mock
@pytest.mark.asyncio
async def test_fetch_extended_prm_uses_www_auth_url_first() -> None:
    """When www_auth_resource_metadata_url is set, it is tried first."""
    custom_url = "https://example.com/custom-prm"
    prm_json = '{"resource": "https://example.com/mcp", "authorization_servers": ["https://auth.example.com"]}'
    respx.get(custom_url).mock(return_value=httpx.Response(200, content=prm_json.encode()))
    result = await fetch_extended_protected_resource_metadata(
        "https://example.com/mcp",
        www_auth_resource_metadata_url=custom_url,
        http_client=AsyncClient(),
    )
    assert result is not None
    assert len(respx.calls) == 1
    assert str(respx.calls[0].request.url) == custom_url


@respx.mock
@pytest.mark.asyncio
async def test_fetch_extended_prm_returns_none_when_all_fail() -> None:
    """Returns None when all discovery URLs return non-200 or invalid JSON."""
    respx.get("https://example.com/.well-known/oauth-protected-resource").mock(
        return_value=httpx.Response(404)
    )
    result = await fetch_extended_protected_resource_metadata(
        "https://example.com",
        http_client=AsyncClient(),
    )
    assert result is None


@respx.mock
@pytest.mark.asyncio
async def test_fetch_extended_prm_returns_none_on_invalid_json() -> None:
    """Returns None when response is 200 but body is not valid PRM JSON."""
    respx.get("https://example.com/.well-known/oauth-protected-resource").mock(
        return_value=httpx.Response(200, content=b"not valid json")
    )
    result = await fetch_extended_protected_resource_metadata(
        "https://example.com",
        http_client=AsyncClient(),
    )
    assert result is None


@respx.mock
@pytest.mark.asyncio
async def test_fetch_extended_prm_fallback_to_next_url() -> None:
    """First URL (path-based) 404s, second (root) returns valid PRM."""
    prm_json = '{"resource": "https://example.com/", "authorization_servers": ["https://auth.example.com"]}'
    respx.get("https://example.com/.well-known/oauth-protected-resource/mcp").mock(
        return_value=httpx.Response(404)
    )
    respx.get("https://example.com/.well-known/oauth-protected-resource").mock(
        return_value=httpx.Response(200, content=prm_json.encode())
    )
    result = await fetch_extended_protected_resource_metadata(
        "https://example.com/mcp",
        http_client=AsyncClient(),
    )
    assert result is not None
