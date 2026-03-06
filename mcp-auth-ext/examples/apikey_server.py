"""Example MCP server with Hello tool and extended PRM at the well-known path.

Requires X-API-Key header for the /mcp endpoint. Run from the mcp-auth-ext directory (terminal 1):

  uv run python examples/apikey_server.py

Listens on http://127.0.0.1:8765. Serves:
- MCP over Streamable HTTP at POST /mcp (tool "hello"), protected by X-API-Key.
- Extended protected resource metadata at /.well-known/oauth-protected-resource/mcp
  with authorization_schemes (api_key).

Then run the client in another terminal (it sends X-API-Key: secret-key).
"""

from __future__ import annotations

import asyncio

import uvicorn
from starlette.types import ASGIApp, Receive, Scope, Send

from mcp.server import FastMCP

from mcp_auth_ext.server import build_extended_prm_payload, create_extended_protected_resource_routes

# API key required for /mcp (must match client)
API_KEY = "secret-key"
HEADER_NAME = "X-API-Key"

# MCP server with Hello tool (v1.x uses FastMCP; streamable_http_path must match route)
server = FastMCP("mcp-auth-ext-demo", debug=True, streamable_http_path="/mcp")


@server.tool()
def hello() -> str:
    """Say hello from the MCP server."""
    return "Hello from MCP!"


class XAPIKeyMiddleware:
    """ASGI middleware that requires X-API-Key header for a given path."""

    def __init__(
        self,
        app: ASGIApp,
        api_key: str,
        path: str = "/mcp",
        header_name: str = "x-api-key",
        resource_metadata_url: str | None = None,
    ) -> None:
        self.app = app
        self.api_key = api_key
        self.path = path
        self.header_name = header_name
        self.resource_metadata_url = resource_metadata_url

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if scope["type"] == "lifespan":
            await self.app(scope, receive, send)
            return
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return
        if scope.get("path") != self.path:
            await self.app(scope, receive, send)
            return
        headers = dict((k.decode().lower(), v.decode()) for k, v in scope.get("headers", []))
        if headers.get(self.header_name) != self.api_key:
            response_headers: list[tuple[bytes, bytes]] = [(b"content-type", b"text/plain")]
            if self.resource_metadata_url:
                # RFC 9728: client can learn PRM URL from 401 WWW-Authenticate resource_metadata
                response_headers.append(
                    (b"www-authenticate", f'Bearer resource_metadata="{self.resource_metadata_url}"'.encode())
                )
            await send(
                {
                    "type": "http.response.start",
                    "status": 401,
                    "headers": response_headers,
                }
            )
            await send({"type": "http.response.body", "body": b"Missing or invalid X-API-Key"})
            return
        await self.app(scope, receive, send)


async def main() -> None:
    host = "127.0.0.1"
    port = 8765
    streamable_http_path = "/mcp"
    resource_url = f"http://{host}:{port}{streamable_http_path}"

    # Add extended PRM routes so clients can discover authorization_schemes
    metadata = build_extended_prm_payload(
        resource_url=resource_url,
        authorization_servers=["https://auth.example.com"],
        authorization_schemes=[
            {"scheme_id": "api_key", "params": {"header_name": HEADER_NAME}},
        ],
    )
    server._custom_starlette_routes.extend(create_extended_protected_resource_routes(metadata))

    # Build app and wrap /mcp with X-API-Key check (we wrap the whole app; middleware checks path)
    starlette_app = server.streamable_http_app()
    # RFC 9728 well-known path for resource at /mcp
    prm_url = f"http://{host}:{port}/.well-known/oauth-protected-resource/mcp"
    wrapped_app = XAPIKeyMiddleware(
        starlette_app,
        api_key=API_KEY,
        path=streamable_http_path,
        header_name="x-api-key",
        resource_metadata_url=prm_url,
    )

    config = uvicorn.Config(wrapped_app, host=host, port=port, log_level="info")
    await uvicorn.Server(config).serve()


if __name__ == "__main__":
    asyncio.run(main())
