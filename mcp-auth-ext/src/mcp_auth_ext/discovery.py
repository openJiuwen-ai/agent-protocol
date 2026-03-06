"""Discovery of extended protected resource metadata."""

import httpx

from mcp.client.auth.utils import build_protected_resource_metadata_discovery_urls
from mcp.client.streamable_http import MCP_PROTOCOL_VERSION
from mcp.types import LATEST_PROTOCOL_VERSION

from mcp_auth_ext.metadata import ExtendedProtectedResourceMetadata


async def fetch_extended_protected_resource_metadata(
    server_url: str,
    *,
    www_auth_resource_metadata_url: str | None = None,
    http_client: httpx.AsyncClient | None = None,
) -> ExtendedProtectedResourceMetadata | None:
    """Fetch extended protected resource metadata from the well-known PRM URL(s).

    Uses the same discovery URL order as the MCP SDK (WWW-Authenticate
    resource_metadata first, then path-based and root-based well-known URIs).
    Parses the response with ExtendedProtectedResourceMetadata so
    authorization_schemes is present when the server includes it.

    Args:
        server_url: The MCP server/resource URL (e.g. https://example.com/mcp).
        www_auth_resource_metadata_url: Optional URL from WWW-Authenticate
            resource_metadata parameter (e.g. after a 401).
        http_client: Optional httpx.AsyncClient. If None, a short-lived client
            is used for the request.

    Returns:
        Extended PRM if discovery succeeded, None if all URLs failed or parsing failed.
    """
    urls = build_protected_resource_metadata_discovery_urls(
        www_auth_resource_metadata_url, server_url
    )
    headers = {MCP_PROTOCOL_VERSION: LATEST_PROTOCOL_VERSION}

    if http_client is not None:
        for url in urls:
            try:
                response = await http_client.get(url, headers=headers)
                if response.status_code != 200:
                    continue
                body = response.content
                return ExtendedProtectedResourceMetadata.model_validate_json(body)
            except Exception:
                continue
        return None

    async with httpx.AsyncClient() as client:
        for url in urls:
            try:
                response = await client.get(url, headers=headers)
                if response.status_code != 200:
                    continue
                body = response.content
                return ExtendedProtectedResourceMetadata.model_validate_json(body)
            except Exception:
                continue
        return None
