"""Server-side helpers to build and serve extended protected resource metadata."""

from __future__ import annotations

from typing import Any
from urllib.parse import urlparse

from pydantic import AnyHttpUrl
from starlette.requests import Request
from starlette.responses import JSONResponse, Response

from mcp.server.auth.routes import build_resource_metadata_url, cors_middleware

from mcp_auth_ext.metadata import AuthSchemeDescriptor, ExtendedProtectedResourceMetadata
from starlette.routing import Route


def build_extended_prm_payload(
    resource_url: str | AnyHttpUrl,
    authorization_servers: list[str] | list[AnyHttpUrl],
    *,
    authorization_schemes: list[AuthSchemeDescriptor | dict[str, Any]] | None = None,
    scopes_supported: list[str] | None = None,
    resource_name: str | None = None,
    resource_documentation: str | AnyHttpUrl | None = None,
) -> ExtendedProtectedResourceMetadata:
    """Build extended protected resource metadata (RFC 9728 + authorization_schemes).

    The result is valid RFC 9728 (SDK-only clients still work) and includes
    optional authorization_schemes for extension-aware clients.

    Args:
        resource_url: The resource server URL (e.g. https://example.com/mcp).
        authorization_servers: List of OAuth authorization server URLs.
        authorization_schemes: Optional list of supported schemes; each item can
            be a dict with scheme_id and optional params (dict of key-value info
            for the client), or AuthSchemeDescriptor.
        scopes_supported: Optional list of scopes (RFC 9728).
        resource_name: Optional resource name (RFC 9728).
        resource_documentation: Optional documentation URL (RFC 9728).

    Returns:
        ExtendedProtectedResourceMetadata suitable for JSON response or
        create_extended_protected_resource_routes.
    """
    resource = AnyHttpUrl(str(resource_url))
    servers = [AnyHttpUrl(str(s)) for s in authorization_servers]
    schemes: list[AuthSchemeDescriptor] | None = None
    if authorization_schemes:
        schemes = [
            s if isinstance(s, AuthSchemeDescriptor) else AuthSchemeDescriptor.model_validate(s)
            for s in authorization_schemes
        ]
    doc_url = AnyHttpUrl(str(resource_documentation)) if resource_documentation else None
    return ExtendedProtectedResourceMetadata(
        resource=resource,
        authorization_servers=servers,
        scopes_supported=scopes_supported,
        resource_name=resource_name,
        resource_documentation=doc_url,
        authorization_schemes=schemes,
    )


def create_extended_protected_resource_routes(
    metadata: ExtendedProtectedResourceMetadata,
) -> list[Route]:
    """Create Starlette routes that serve extended PRM at the RFC 9728 well-known path.

    Use these routes in place of (or in addition to) the SDK's
    create_protected_resource_routes when you want to advertise
    authorization_schemes. The same document is valid RFC 9728.

    Args:
        metadata: Extended PRM (e.g. from build_extended_prm_payload).

    Returns:
        List of Starlette Route; mount on your app.
    """
    metadata_url = build_resource_metadata_url(metadata.resource)
    parsed = urlparse(str(metadata_url))
    well_known_path = parsed.path

    async def handle(_request: Request) -> Response:
        return JSONResponse(
            metadata.model_dump(mode="json"),
            headers={"Cache-Control": "public, max-age=3600"},
        )

    return [
        Route(
            well_known_path,
            endpoint=cors_middleware(handle, ["GET", "OPTIONS"]),
            methods=["GET", "OPTIONS"],
        )
    ]
