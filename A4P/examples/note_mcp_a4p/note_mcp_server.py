"""A note-management MCP server protected by A4P authorization."""

from __future__ import annotations

from typing import Any

from a4p import A4PClient
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
_operation_results: dict[str, dict[str, Any]] = {}


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
    intent_token: dict[str, Any] | None,
) -> tuple[bool, str]:
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

    return False, "delete_note requires A4P authorization"


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
    operation_authorization: dict[str, Any] | None = None,
    intent_token: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Delete a note after A4P authorization."""
    note = _notes.get(note_id)
    if note is None:
        return {"deleted": False, "error": f"note not found: {note_id}"}

    operation_id: str | None = None
    if operation_authorization is not None:
        signed_mandate = operation_authorization.get("signedMandate")
        if not isinstance(signed_mandate, dict):
            return {
                "deleted": False,
                "error": "A4P authorization failed: signedMandate missing",
                "note": _summary(note),
            }
        completed = await A4PClient().complete_operation_authorization(
            {
                "signedMandate": signed_mandate,
                "operation": _delete_operation(note_id),
            }
        )
        authorized = completed.approved and completed.operationId is not None
        reason = completed.rejectReason or ""
        operation_id = completed.operationId
    elif intent_token is not None:
        authorized, reason = await _authorize_delete(note_id, intent_token=intent_token)
    else:
        challenge = await A4PClient().prepare_operation_authorization(
            {
                "agentId": AGENT_ID,
                "userId": USER_ID,
                "operation": _delete_operation(note_id),
                "validitySeconds": 300,
                "metadata": {"noteTitle": note["title"]},
            }
        )
        if challenge.mandate is None:
            return {
                "deleted": False,
                "error": f"A4P authorization preparation failed: {challenge.rejectReason}",
                "note": _summary(note),
            }
        return {
            "deleted": False,
            "status": "authorization_required",
            "authorization": {
                "mandate": challenge.mandate,
                "signingOptions": challenge.signingOptions,
            },
            "note": _summary(note),
        }

    if not authorized:
        return {
            "deleted": False,
            "error": f"A4P authorization failed: {reason}",
            "note": _summary(note),
        }

    if operation_id is not None and operation_id in _operation_results:
        return dict(_operation_results[operation_id])

    deleted = _notes.pop(note_id)
    result = {
        "deleted": True,
        "note": _summary(deleted),
        "remaining": list_notes(),
    }
    if operation_id is not None:
        result["operationId"] = operation_id
        _operation_results[operation_id] = dict(result)
    return result


if __name__ == "__main__":
    mcp.run()
