# A4P + MCP Note Management Example

This example shows a note-management MCP server protected by A4P authorization.

The MCP server exposes four tools:

- `list_notes`
- `get_note`
- `add_note`
- `delete_note`

`delete_note` is treated as a high-risk operation. It only deletes after receiving either:

- an A4P operation mandate for the exact note deletion, or
- an A4P intent token that permits deleting notes.

## Install

From the A4P SDK directory:

```bash
uv sync
```

## Run With Per-Operation Authorization

Terminal 1:

```bash
uv run python examples/note_mcp_a4p/run_authorization_server.py
```

Open the printed local web UI, usually:

```text
http://127.0.0.1:8970/
```

When an operation or intent authorization request arrives, the authorizer opens
the browser directly to that request. Use `--no-open-browser` if you only want
the URL printed in the terminal:

```bash
uv run python examples/note_mcp_a4p/run_authorization_server.py --no-open-browser
```

Terminal 2:

```bash
uv run python examples/note_mcp_a4p/agent_simulator.py --mode operation
```

The simulated agent lists all notes, then asks for one A4P operation
authorization per delete. Approve or reject each request in the web UI.

Operation authorization uses one exact tool call:

```json
{
  "operation": {
    "action": "delete_note",
    "params": {
      "note_id": "note-1"
    }
  }
}
```

## Run With Intent Authorization

Terminal 1:

```bash
uv run python examples/note_mcp_a4p/run_authorization_server.py
```

Terminal 2:

```bash
uv run python examples/note_mcp_a4p/agent_simulator.py --mode intent
```

The simulated agent requests one A4P intent token that permits deleting notes,
then reuses that token while deleting the listed notes.

Intent authorization uses reusable action constraints:

```json
{
  "intent": {
    "actions": [
      {
        "name": "delete_note",
        "params": {
          "note_id": "*"
        }
      }
    ]
  }
}
```

## Smoke Test

The smoke test uses `ApprovingA4PUserAuthorizer` and does not require the web UI:

```bash
uv run python examples/note_mcp_a4p/smoke_test.py
```

It verifies:

- operation authorization can delete one note;
- one intent token can delete multiple notes;
- deletion without A4P authorization fails and preserves the note.
