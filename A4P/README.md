# A4P Python SDK

A4P is a lightweight Python SDK for agent authorization mandates and tokens.

## Install for local development

```bash
uv pip install -e .
```

## Package Layout

```text
src/a4p/
  __init__.py
  client.py
  server.py
  http_server.py
  security.py
  intent_mandate.py
  operation_mandate.py
  types.py
  user_authorizer.py
```

## Quick Import

```python
from a4p import A4PClient, A4PServer
```

## Examples

- `examples/note_mcp_a4p`: a note-management MCP server that uses A4P operation and intent authorization before deleting notes.

See `A4P_SDK_DESIGN.md` for the current design and interface summary.
