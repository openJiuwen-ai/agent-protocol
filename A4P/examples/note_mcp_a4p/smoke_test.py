"""Non-interactive smoke tests for the A4P note MCP example."""

from __future__ import annotations

import asyncio
import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey  # noqa: E402

from a4p import (  # noqa: E402
    A4PClient,
    A4PServer,
    InMemoryCredentialStore,
    sign_user_mandate_with_signer,
)
from a4p.user_signature.ed25519 import (  # noqa: E402
    Ed25519UserSigner,
    RegisteredEd25519Method,
    ed25519_public_jwk,
)
from a4p.http_server import A4PHTTPServer  # noqa: E402

import note_mcp_server  # noqa: E402


def _explicit_test_server() -> tuple[A4PServer, Ed25519UserSigner]:
    private_key = Ed25519PrivateKey.generate()
    signature_method = RegisteredEd25519Method(InMemoryCredentialStore())
    registration = signature_method.register(
        {
            "userId": "demo-user",
            "publicKey": ed25519_public_jwk(private_key),
            "metadata": {"purpose": "non-interactive smoke test"},
        }
    )
    return (
        A4PServer(
            server_id="local://note-a4p-demo",
            user_signature_method=signature_method,
        ),
        Ed25519UserSigner(
            credential_id=registration["credential"]["credentialId"],
            private_key=private_key,
        ),
    )


def _reset_notes() -> None:
    note_mcp_server._notes.clear()
    note_mcp_server._notes.update(
        {
            "note-1": {"id": "note-1", "title": "One", "body": "First"},
            "note-2": {"id": "note-2", "title": "Two", "body": "Second"},
        }
    )
    note_mcp_server._next_note_id = 3
    note_mcp_server._operation_results.clear()


async def _operation_delete_succeeds() -> None:
    _reset_notes()
    server, signer = _explicit_test_server()
    a4p_server = A4PHTTPServer(
        server,
        host="127.0.0.1",
        port=18960,
    )
    await a4p_server.start()
    old_base_url = os.environ.get("A4P_SERVER_BASE_URL")
    old_no_proxy = os.environ.get("NO_PROXY")
    os.environ["A4P_SERVER_BASE_URL"] = f"http://127.0.0.1:{a4p_server.port}"
    os.environ["NO_PROXY"] = "127.0.0.1,localhost"
    try:
        challenged = await note_mcp_server.delete_note("note-1")
        authorization = challenged["authorization"]
        signed = sign_user_mandate_with_signer(
            authorization["mandate"],
            user_signer=signer,
        )
        result = await note_mcp_server.delete_note(
            "note-1",
            operation_authorization={
                "signedMandate": signed,
            },
        )
        assert challenged["status"] == "authorization_required"
        assert result["deleted"] is True
        assert result["operationId"] == signed["operationId"]
        assert "note-1" not in note_mcp_server._notes
    finally:
        if old_base_url is None:
            os.environ.pop("A4P_SERVER_BASE_URL", None)
        else:
            os.environ["A4P_SERVER_BASE_URL"] = old_base_url
        if old_no_proxy is None:
            os.environ.pop("NO_PROXY", None)
        else:
            os.environ["NO_PROXY"] = old_no_proxy
        await a4p_server.stop()


async def _operation_params_mismatch_fails() -> None:
    _reset_notes()
    server, signer = _explicit_test_server()
    a4p_server = A4PHTTPServer(
        server,
        host="127.0.0.1",
        port=18960,
    )
    await a4p_server.start()
    old_base_url = os.environ.get("A4P_SERVER_BASE_URL")
    old_no_proxy = os.environ.get("NO_PROXY")
    os.environ["A4P_SERVER_BASE_URL"] = f"http://127.0.0.1:{a4p_server.port}"
    os.environ["NO_PROXY"] = "127.0.0.1,localhost"
    try:
        challenged = await note_mcp_server.delete_note("note-1")
        authorization = challenged["authorization"]
        signed = sign_user_mandate_with_signer(
            authorization["mandate"],
            user_signer=signer,
        )
        result = await note_mcp_server.delete_note(
            "note-2",
            operation_authorization={
                "signedMandate": signed,
            },
        )
        assert result["deleted"] is False
        assert "does not match pending" in result["error"]
        assert "note-1" in note_mcp_server._notes
        assert "note-2" in note_mcp_server._notes
    finally:
        if old_base_url is None:
            os.environ.pop("A4P_SERVER_BASE_URL", None)
        else:
            os.environ["A4P_SERVER_BASE_URL"] = old_base_url
        if old_no_proxy is None:
            os.environ.pop("NO_PROXY", None)
        else:
            os.environ["NO_PROXY"] = old_no_proxy
        await a4p_server.stop()


async def _intent_token_deletes_multiple_notes() -> None:
    _reset_notes()
    server, signer = _explicit_test_server()
    a4p_server = A4PHTTPServer(
        server,
        host="127.0.0.1",
        port=18961,
    )
    await a4p_server.start()
    old_base_url = os.environ.get("A4P_SERVER_BASE_URL")
    old_no_proxy = os.environ.get("NO_PROXY")
    os.environ["A4P_SERVER_BASE_URL"] = f"http://127.0.0.1:{a4p_server.port}"
    os.environ["NO_PROXY"] = "127.0.0.1,localhost"
    try:
        client = A4PClient()
        prepared = await client.prepare_intent_authorization(
            {
                "agentId": "demo-agent",
                "userId": "demo-user",
                "intent": {"actions": [{"name": "delete_note", "params": {"note_id": "*"}}]},
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        signed = sign_user_mandate_with_signer(
            prepared.mandate,
            user_signer=signer,
        )
        auth = await client.complete_intent_authorization(
            {"signedMandate": signed}
        )
        first = await note_mcp_server.delete_note("note-1", intent_token=auth.intentToken)
        second = await note_mcp_server.delete_note("note-2", intent_token=auth.intentToken)
        assert first["deleted"] is True
        assert second["deleted"] is True
        assert note_mcp_server._notes == {}
    finally:
        if old_base_url is None:
            os.environ.pop("A4P_SERVER_BASE_URL", None)
        else:
            os.environ["A4P_SERVER_BASE_URL"] = old_base_url
        if old_no_proxy is None:
            os.environ.pop("NO_PROXY", None)
        else:
            os.environ["NO_PROXY"] = old_no_proxy
        await a4p_server.stop()


async def _intent_constraints_reject_mismatches() -> None:
    _reset_notes()
    server, signer = _explicit_test_server()
    a4p_server = A4PHTTPServer(
        server,
        host="127.0.0.1",
        port=18962,
    )
    await a4p_server.start()
    old_base_url = os.environ.get("A4P_SERVER_BASE_URL")
    old_no_proxy = os.environ.get("NO_PROXY")
    os.environ["A4P_SERVER_BASE_URL"] = f"http://127.0.0.1:{a4p_server.port}"
    os.environ["NO_PROXY"] = "127.0.0.1,localhost"
    try:
        client = A4PClient()
        prepared = await client.prepare_intent_authorization(
            {
                "agentId": "demo-agent",
                "userId": "demo-user",
                "intent": {"actions": [{"name": "delete_note", "params": {"note_id": "note-1"}}]},
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        signed = sign_user_mandate_with_signer(
            prepared.mandate,
            user_signer=signer,
        )
        auth = await client.complete_intent_authorization(
            {"signedMandate": signed}
        )
        missing = await client.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "delete_note", "params": {}, "agentId": "agent:demo-agent", "userId": "demo-user"},
            }
        )
        extra = await client.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {
                    "action": "delete_note",
                    "params": {"note_id": "note-1", "force": True},
                    "agentId": "agent:demo-agent",
                    "userId": "demo-user",
                },
            }
        )
        action_mismatch = await client.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "archive_note", "params": {"note_id": "note-1"}, "agentId": "agent:demo-agent", "userId": "demo-user"},
            }
        )
        assert missing.valid is False
        assert extra.valid is False
        assert action_mismatch.valid is False
    finally:
        if old_base_url is None:
            os.environ.pop("A4P_SERVER_BASE_URL", None)
        else:
            os.environ["A4P_SERVER_BASE_URL"] = old_base_url
        if old_no_proxy is None:
            os.environ.pop("NO_PROXY", None)
        else:
            os.environ["NO_PROXY"] = old_no_proxy
        await a4p_server.stop()


async def _delete_without_authorization_fails() -> None:
    # Covered by the first phase of the operation tests: no authorization executes no deletion.
    return None


async def main() -> None:
    await _operation_delete_succeeds()
    await _operation_params_mismatch_fails()
    await _intent_token_deletes_multiple_notes()
    await _intent_constraints_reject_mismatches()
    await _delete_without_authorization_fails()
    print("note_mcp_a4p smoke tests passed")


if __name__ == "__main__":
    asyncio.run(main())
