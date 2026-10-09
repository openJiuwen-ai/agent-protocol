"""Simulate an AI agent that deletes notes through MCP after A4P authorization."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from a4p import A4PClient
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


AGENT_ID = "demo-agent"
USER_ID = "demo-user"
PROJECT_ROOT = Path(__file__).resolve().parents[2]
SERVER_SCRIPT = Path(__file__).with_name("note_mcp_server.py")
USER_AUTHORIZER_BASE_URL = (os.getenv("A4P_USER_AUTHORIZER_BASE_URL") or "http://localhost:8970").rstrip("/")


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


async def _request_user_signature(
    *,
    mandate: dict[str, Any],
    signing_options: dict[str, Any],
) -> dict[str, Any]:
    payload = {
        "mandate": mandate,
        "signingOptions": signing_options,
    }
    return await asyncio.to_thread(_post_user_authorizer_sync, "/authorize", payload)


def _post_user_authorizer_sync(path: str, payload: dict[str, Any]) -> dict[str, Any]:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        f"{USER_AUTHORIZER_BASE_URL}{path}",
        data=data,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            raw = resp.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"User Authorizer HTTP {exc.code}: {raw}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"User Authorizer HTTP request failed: {exc}") from exc
    parsed = json.loads(raw) if raw else {}
    return parsed if isinstance(parsed, dict) else {}


async def _delete_with_operation(
    session: ClientSession,
    notes: list[dict[str, Any]],
) -> None:
    for note in notes:
        note_id = str(note["id"])
        print(f"\n[agent] requesting operation authorization for {note_id}")
        challenged = await _call_tool(session, "delete_note", {"note_id": note_id})
        authorization = challenged.get("authorization") if isinstance(challenged, dict) else None
        if not isinstance(authorization, dict) or not isinstance(authorization.get("mandate"), dict):
            print(f"[agent] operation challenge failed for {note_id}: {challenged}")
            continue
        user_auth = await _request_user_signature(
            mandate=authorization["mandate"],
            signing_options=(
                authorization.get("signingOptions")
                if isinstance(authorization.get("signingOptions"), dict)
                else {}
            ),
        )
        if not user_auth.get("approved") or not isinstance(user_auth.get("signedMandate"), dict):
            print(f"[agent] user authorization rejected for {note_id}: {user_auth.get('rejectReason')}")
            continue
        deleted = await _call_tool(
            session,
            "delete_note",
            {
                "note_id": note_id,
                "operation_authorization": {
                    "signedMandate": user_auth["signedMandate"],
                },
            },
        )
        print(f"[agent] delete result: {json.dumps(deleted, ensure_ascii=False)}")


async def _delete_with_intent(session: ClientSession, a4p_client: A4PClient, notes: list[dict[str, Any]]) -> None:
    print("\n[agent] requesting one intent authorization for deleting notes")
    prepared = await a4p_client.prepare_intent_authorization(
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
    if prepared.mandate is None:
        print(f"[agent] intent mandate rejected: {prepared.rejectReason}")
        return
    user_auth = await _request_user_signature(
        mandate=prepared.mandate,
        signing_options=prepared.signingOptions,
    )
    if not user_auth.get("approved") or not isinstance(user_auth.get("signedMandate"), dict):
        print(f"[agent] user intent authorization rejected: {user_auth.get('rejectReason')}")
        return
    auth = await a4p_client.complete_intent_authorization(
        {"signedMandate": user_auth["signedMandate"]}
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
                await _delete_with_operation(session, notes)
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
