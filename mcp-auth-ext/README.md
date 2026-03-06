# mcp-auth-ext

Extended authorization for the [MCP](https://modelcontextprotocol.io) Python SDK. This package depends on `mcp` and adds:

- **Extended protected resource metadata** — Optional `authorization_schemes` in the same RFC 9728 document so clients can discover supported schemes (OAuth 2.1, API key, etc.) from the well-known PRM endpoint.
- **Discovery API** — `fetch_extended_protected_resource_metadata()` to retrieve extended PRM using the same URL order as the SDK.
- **Server helpers** — Build extended PRM payloads and an optional Starlette route to serve them at `/.well-known/oauth-protected-resource...`.

No changes to the MCP SDK; Backward compatible.

## Installation

```bash
uv sync
```

## Usage

### Client: fetch extended metadata

```python
from mcp_auth_ext import fetch_extended_protected_resource_metadata

# After a 401, optionally pass resource_metadata URL from WWW-Authenticate
prm = await fetch_extended_protected_resource_metadata(
    "https://example.com/mcp",
    www_auth_resource_metadata_url=None,
)
if prm.authorization_schemes:
    for scheme in prm.authorization_schemes:
        print(scheme.scheme_id, scheme.params)
# Choose auth (e.g. SDK OAuthClientProvider or custom from examples) and retry
```

### Server: build and serve extended PRM

```python
from mcp_auth_ext.server import build_extended_prm_payload, create_extended_protected_resource_routes

payload = build_extended_prm_payload(
    resource_url="https://example.com/mcp",
    authorization_servers=["https://auth.example.com"],
    authorization_schemes=[
        {"scheme_id": "api_key", "params": {"header_name": "X-API-Key"}},
    ],
)
routes = create_extended_protected_resource_routes(payload)
# Mount routes on your Starlette app
```

## Examples

### Run server and client

From the `mcp-auth-ext` directory, start the MCP server in one terminal:

```bash
uv run python examples/server.py
```

The server listens on http://127.0.0.1:8765, serves MCP over Streamable HTTP at `/mcp` with a **hello** tool, and extended PRM at `/.well-known/oauth-protected-resource/mcp` (with `authorization_schemes`: api_key).

In a second terminal, run the client:

```bash
uv run python examples/client.py
```

The client discovers the extended PRM, connects to the MCP server, and calls the **hello** tool. Expected output: `Hello from MCP!`

## License

MIT
