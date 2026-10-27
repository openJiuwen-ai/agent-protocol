"""Simulate an AI agent that deletes notes through MCP after A4P authorization."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from pathlib import Path
from typing import Any

from a4p import A4PClient
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


AGENT_ID = "demo-agent"
USER_ID = "demo-user"
PROJECT_ROOT = Path(__file__).resolve().parents[2]
SERVER_SCRIPT = Path(__file__).with_name("note_mcp_server.py")


def _delete_operation(note_id: str) -> dict[str, Any]:
    return {
        "action": "delete_note",
        "params": {"note_id": note_id},
    }


def _server_env() -> dict[str, str]:
    env = dict(os.environ)
    src_path = str(PROJECT_ROOT / "src")
    existing = env.get("PYTHONPATH")
    env["PYTHONPATH"] = f"{src_path}{os.pathsep}{existing}" if existing else src_path
    return env


def _tool_payload(result: Any) -> Any:
    structured = getattr(result, "structuredContent", None)
    if structured is None:
        structured = getattr(result, "structured_content", None)
    if isinstance(structured, dict) and set(structured) == {"result"}:
        return structured["result"]
    if structured is not None:
        return structured

    for content in getattr(result, "content", []) or []:
        text = getattr(content, "text", None)
        if not text:
            continue
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text
    return None


async def _call_tool(session: ClientSession, name: str, arguments: dict[str, Any] | None = None) -> Any:
    result = await session.call_tool(name, arguments or {})
    return _tool_payload(result)


async def _delete_with_operation(session: ClientSession, a4p_client: A4PClient, notes: list[dict[str, Any]]) -> None:
    for note in notes:
        note_id = str(note["id"])
        print(f"\n[agent] requesting operation authorization for {note_id}")
        auth = await a4p_client.request_operation_authorization(
            {
                "agentId": AGENT_ID,
                "userId": USER_ID,
                "operation": _delete_operation(note_id),
                "validitySeconds": 300,
                "metadata": {"noteTitle": note.get("title", "")},
            }
        )
        if not auth.approved or auth.signedMandate is None:
            print(f"[agent] authorization rejected for {note_id}: {auth.rejectReason}")
            continue
        deleted = await _call_tool(
            session,
            "delete_note",
            {"note_id": note_id, "operation_mandate": auth.signedMandate},
        )
        print(f"[agent] delete result: {json.dumps(deleted, ensure_ascii=False)}")


async def _delete_with_intent(session: ClientSession, a4p_client: A4PClient, notes: list[dict[str, Any]]) -> None:
    print("\n[agent] requesting one intent authorization for deleting notes")
    auth = await a4p_client.request_intent_authorization(
        {
            "agentId": AGENT_ID,
            "userId": USER_ID,
            "intent": {
                "actions": [
                    {
                        "name": "delete_note",
                        "params": {"note_id": "*"},
                    }
                ],
            },
            "validitySeconds": 600,
            "metadata": {"reason": "The simulated agent wants to delete all listed notes."},
        }
    )
    if not auth.approved or auth.intentToken is None:
        print(f"[agent] intent authorization rejected: {auth.rejectReason}")
        return

    for note in notes:
        note_id = str(note["id"])
        deleted = await _call_tool(
            session,
            "delete_note",
            {"note_id": note_id, "intent_token": auth.intentToken},
        )
        print(f"[agent] delete result for {note_id}: {json.dumps(deleted, ensure_ascii=False)}")


async def run(mode: str) -> None:
    a4p_client = A4PClient()
    server = StdioServerParameters(
        command=sys.executable,
        args=[str(SERVER_SCRIPT)],
        env=_server_env(),
    )
    async with stdio_client(server) as (read_stream, write_stream):
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()

            notes = await _call_tool(session, "list_notes")
            print(f"[agent] listed notes: {json.dumps(notes, ensure_ascii=False)}")
            if not notes:
                print("[agent] no notes to delete")
                return

            if mode == "operation":
                await _delete_with_operation(session, a4p_client, notes)
            else:
                await _delete_with_intent(session, a4p_client, notes)

            remaining = await _call_tool(session, "list_notes")
            print(f"\n[agent] remaining notes: {json.dumps(remaining, ensure_ascii=False)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode",
        choices=("operation", "intent"),
        default="operation",
        help="Authorization mode used before deleting notes.",
    )
    args = parser.parse_args()
    asyncio.run(run(args.mode))


if __name__ == "__main__":
    main()
