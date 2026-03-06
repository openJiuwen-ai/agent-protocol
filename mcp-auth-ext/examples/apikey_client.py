"""Example client: discovers extended PRM (from 401 WWW-Authenticate or well-known), then connects with X-API-Key.

If the first attempt to the server returns 401, the client learns the PRM URL from the
WWW-Authenticate resource_metadata parameter and fetches PRM from there.

Run from the mcp-auth-ext directory (terminal 2, after starting the server):

  uv run python examples/apikey_client.py

Optional: pass the MCP server URL and API key (defaults: http://127.0.0.1:8765/mcp, secret-key):

  uv run python examples/apikey_client.py http://127.0.0.1:8765/mcp secret-key
"""

from __future__ import annotations

import asyncio
import sys
import typing

import httpx

from mcp.client.auth.utils import extract_resource_metadata_from_www_auth
from mcp.client.session import ClientSession
from mcp.client.streamable_http import streamable_http_client

from mcp_auth_ext import fetch_extended_protected_resource_metadata

# Use the same header auth as in examples/header_auth.py (minimal version here)
DEFAULT_API_KEY = "secret-key"
HEADER_NAME = "X-API-Key"


class _HeaderAuth(httpx.Auth):
    """Adds an API key header to every request (e.g. X-API-Key)."""

    def __init__(self, header_name: str, value: str) -> None:
        self._header_name = header_name
        self._value = value

    def auth_flow(self, request: httpx.Request) -> typing.Generator[httpx.Request, httpx.Response, None]:
        request.headers[self._header_name] = self._value
        yield request

    async def async_auth_flow(self, request: httpx.Request) -> typing.AsyncGenerator[httpx.Request, httpx.Response]:
        request.headers[self._header_name] = self._value
        yield request


async def main(server_url: str, api_key: str) -> None:
    # Phase 1: Learn PRM URL from 401 (if first attempt fails) or from well-known
    prm_url_from_401: str | None = None
    async with httpx.AsyncClient() as no_auth_client:
        # Try a request that may return 401 (e.g. POST to MCP endpoint without auth)
        probe = await no_auth_client.post(server_url, json={})
        if probe.status_code == 401:
            prm_url_from_401 = extract_resource_metadata_from_www_auth(probe)
            if prm_url_from_401:
                print(f"Learned PRM URL from 401: {prm_url_from_401}")

    # Phase 2: Fetch extended PRM (use URL from 401 first if we got it)
    async with httpx.AsyncClient() as http_client:
        prm = await fetch_extended_protected_resource_metadata(
            server_url,
            www_auth_resource_metadata_url=prm_url_from_401,
            http_client=http_client,
        )
    if prm:
        if prm.authorization_schemes:
            print("Fetched PRM. Server supports the following authorization schemes:")
            for s in prm.authorization_schemes:
                line = f"  - {s.scheme_id}"
                if s.params:
                    line += f" (params: {s.params})"
                print(line)
    else:
        print(
            "Could not fetch extended PRM (is the server running? "
            "Start it with: uv run python examples/apikey_server.py)"
        )

    # Phase 3: Connect to MCP with X-API-Key and call the hello tool
    async with httpx.AsyncClient(auth=_HeaderAuth(HEADER_NAME, api_key)) as http_client:
        async with streamable_http_client(server_url, http_client=http_client) as (
            read_stream,
            write_stream,
            _,
        ):
            async with ClientSession(read_stream, write_stream) as session:
                await session.initialize()
                result = await session.call_tool("hello", {})
    if result.content:
        for block in result.content:
            if hasattr(block, "text"):
                print(block.text)
                break
    else:
        print(result)


if __name__ == "__main__":
    default_url = "http://127.0.0.1:8765/mcp"
    server_url = sys.argv[1] if len(sys.argv) > 1 else default_url
    api_key = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_API_KEY
    asyncio.run(main(server_url, api_key))
