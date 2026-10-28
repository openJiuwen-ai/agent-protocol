"""A note-management MCP server protected by A4P authorization."""

from __future__ import annotations

from typing import Any

from a4p import A4PClient
from a4p.operation_mandate import verify_operation_mandate
from mcp.server.fastmcp import FastMCP


AGENT_ID = "demo-agent"
USER_ID = "demo-user"

mcp = FastMCP("A4P Note MCP Server")

_notes: dict[str, dict[str, str]] = {
    "note-1": {
        "id": "note-1",
        "title": "Project checklist",
        "body": "Ship the A4P package layout and examples.",
    },
    "note-2": {
        "id": "note-2",
        "title": "Meeting notes",
        "body": "Deletion should require explicit user authorization.",
    },
    "note-3": {
        "id": "note-3",
        "title": "Draft",
        "body": "This note exists so the simulated agent has another target.",
    },
}
_next_note_id = 4


def _summary(note: dict[str, str]) -> dict[str, str]:
    return {"id": note["id"], "title": note["title"]}


def _delete_operation(note_id: str) -> dict[str, Any]:
    return {
        "action": "delete_note",
        "params": {"note_id": note_id},
    }


async def _authorize_delete(
    note_id: str,
    *,
    operation_mandate: dict[str, Any] | None,
    intent_token: dict[str, Any] | None,
) -> tuple[bool, str]:
    if operation_mandate is not None:
        valid, reason = verify_operation_mandate(
            operation_mandate,
            expected=_delete_operation(note_id),
        )
        return valid, reason

    if intent_token is not None:
        response = await A4PClient().verify_intent_token(
            {
                "token": intent_token,
                "expected": {
                    "action": "delete_note",
                    "params": {"note_id": note_id},
                    "agentId": f"agent:{AGENT_ID}",
                    "userId": USER_ID,
                },
            }
        )
        return response.valid, response.reason or ""

    return False, "delete_note requires an A4P operation mandate or intent token"


@mcp.tool()
def list_notes() -> list[dict[str, str]]:
    """List note ids and titles."""
    return [_summary(note) for note in _notes.values()]


@mcp.tool()
def get_note(note_id: str) -> dict[str, Any]:
    """Return one note by id."""
    note = _notes.get(note_id)
    if note is None:
        return {"found": False, "error": f"note not found: {note_id}"}
    return {"found": True, "note": dict(note)}


@mcp.tool()
def add_note(title: str, body: str) -> dict[str, Any]:
    """Add a note and return the created entry."""
    global _next_note_id

    note_id = f"note-{_next_note_id}"
    _next_note_id += 1
    note = {"id": note_id, "title": title, "body": body}
    _notes[note_id] = note
    return {"created": True, "note": dict(note)}


@mcp.tool()
async def delete_note(
    note_id: str,
    operation_mandate: dict[str, Any] | None = None,
    intent_token: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Delete a note after A4P authorization."""
    note = _notes.get(note_id)
    if note is None:
        return {"deleted": False, "error": f"note not found: {note_id}"}

    authorized, reason = await _authorize_delete(
        note_id,
        operation_mandate=operation_mandate,
        intent_token=intent_token,
    )
    if not authorized:
        return {
            "deleted": False,
            "error": f"A4P authorization failed: {reason}",
            "note": _summary(note),
        }

    deleted = _notes.pop(note_id)
    return {
        "deleted": True,
        "note": _summary(deleted),
        "remaining": list_notes(),
    }


if __name__ == "__main__":
    mcp.run()
