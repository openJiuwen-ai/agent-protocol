from __future__ import annotations

import asyncio

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p import A4PClient
from a4p.user_signature.ed25519 import ed25519_public_jwk
from a4p.http_server import A4PHTTPServer, a4p_http_port
from tests.support import ExplicitEd25519A4PServer as A4PServer
from tests.support import sign_user_mandate


async def _raw_http_request(port: int, request: bytes) -> bytes:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request)
    await writer.drain()
    response = await reader.read()
    writer.close()
    await writer.wait_closed()
    return response


def test_dispatch_returns_404_for_unknown_path() -> None:
    server = A4PHTTPServer(object())  # type: ignore[arg-type]

    status, payload = asyncio.run(server._dispatch("/missing", {}))

    assert status == 404
    assert payload == {"error": "not_found"}


def test_a4p_http_port_rejects_invalid_value(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("A4P_SERVER_PORT", "not-a-port")

    with pytest.raises(ValueError, match="A4P_SERVER_PORT must be an integer"):
        a4p_http_port()


def test_a4p_http_port_uses_default_when_unset() -> None:
    assert a4p_http_port() == 8961


def test_http_prepare_complete_operation_endpoint() -> None:
    async def run() -> None:
        server = A4PHTTPServer(
            A4PServer(server_id="local://test"), host="127.0.0.1", port=0
        )
        prepare_status, prepared = await server._dispatch(
            "/a4p/v1/operation-authorizations/prepare",
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": {"action": "delete_note", "params": {"note_id": "note-1"}},
                "validitySeconds": 60,
            },
        )
        signed = sign_user_mandate(prepared["mandate"])
        complete_status, completed = await server._dispatch(
            "/a4p/v1/operation-authorizations/complete",
            {
                "signedMandate": signed,
                "operation": {"action": "delete_note", "params": {"note_id": "note-1"}},
            },
        )

        assert prepare_status == 200
        assert complete_status == 200
        assert completed["approved"] is True
        assert completed["operationId"] == signed["operationId"]
        assert "signedMandate" not in completed

    asyncio.run(run())


def test_http_verify_intent_token_consumes_execution_policy_quota() -> None:
    async def run() -> None:
        server = A4PHTTPServer(
            A4PServer(server_id="local://test"), host="127.0.0.1", port=0
        )
        prepare_status, prepared = await server._dispatch(
            "/a4p/v1/intent-authorizations/prepare",
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}],
                    "executionPolicy": {"maxExecutions": 1},
                },
                "validitySeconds": 60,
            },
        )
        signed = sign_user_mandate(prepared["mandate"])
        complete_status, completed = await server._dispatch(
            "/a4p/v1/intent-authorizations/complete",
            {"signedMandate": signed},
        )
        first_status, first = await server._dispatch(
            "/a4p/v1/intent-tokens/verify",
            {
                "token": completed["intentToken"],
                "expected": {"action": "delete_note", "params": {"note_id": "note-1"}},
            },
        )
        second_status, second = await server._dispatch(
            "/a4p/v1/intent-tokens/verify",
            {
                "token": completed["intentToken"],
                "expected": {"action": "delete_note", "params": {"note_id": "note-2"}},
            },
        )

        assert prepare_status == 200
        assert complete_status == 200
        assert first_status == 200
        assert second_status == 200
        assert first["valid"] is True
        assert first["matchedScope"]["usage"]["executionsUsed"] == 1
        assert second["valid"] is False
        assert second["code"] == "TOKEN_USAGE_EXCEEDED"

    asyncio.run(run())


def test_a4p_client_uses_real_http_server_for_public_authorization_apis(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("NO_PROXY", "127.0.0.1,localhost")
    monkeypatch.setenv("no_proxy", "127.0.0.1,localhost")

    async def run() -> None:
        http_server = A4PHTTPServer(
            A4PServer(server_id="local://http-client-test"),
            host="127.0.0.1",
            port=0,
        )
        assert http_server.port == 0
        await http_server.start()
        client = A4PClient(
            base_url=f"http://127.0.0.1:{http_server.port}/",
            timeout=2,
        )
        try:
            operation_prepared = await client.prepare_operation_authorization(
                {
                    "agentId": "agent-1",
                    "userId": "user-1",
                    "operation": {
                        "action": "delete_note",
                        "params": {"note_id": "note-1"},
                    },
                    "validitySeconds": 60,
                }
            )
            operation_signed = sign_user_mandate(operation_prepared.mandate)
            operation_completed = await client.complete_operation_authorization(
                {
                    "signedMandate": operation_signed,
                    "operation": {
                        "action": "delete_note",
                        "params": {"note_id": "note-1"},
                    },
                }
            )

            intent_prepared = await client.prepare_intent_authorization(
                {
                    "agentId": "agent-1",
                    "userId": "user-1",
                    "intent": {
                        "actions": [
                            {
                                "name": "delete_note",
                                "params": {"note_id": "*"},
                            }
                        ]
                    },
                    "validitySeconds": 60,
                }
            )
            intent_signed = sign_user_mandate(intent_prepared.mandate)
            intent_completed = await client.complete_intent_authorization(
                {"signedMandate": intent_signed}
            )
            token_verified = await client.verify_intent_token(
                {
                    "token": intent_completed.intentToken,
                    "expected": {
                        "action": "delete_note",
                        "params": {"note_id": "note-2"},
                    },
                }
            )
            registered_public_key = ed25519_public_jwk(
                Ed25519PrivateKey.generate()
            )
            registration = await client.register_ed25519_credential(
                {
                    "userId": "user-2",
                    "publicKey": registered_public_key,
                }
            )

            assert operation_completed.approved is True
            assert operation_completed.operationId == operation_signed["operationId"]
            assert intent_completed.approved is True
            assert token_verified.valid is True
            assert registration["created"] is True
            assert registration["credential"]["credentialId"].startswith(
                "cred_"
            )

            with pytest.raises(RuntimeError, match="A4P HTTP 400.*bad_request"):
                await client.register_ed25519_credential(
                    {
                        "userId": "user-3",
                        "publicKey": {
                            "kty": "OKP",
                            "crv": "Ed25519",
                            "x": "invalid",
                        },
                    }
                )
            with pytest.raises(
                RuntimeError,
                match="A4P HTTP 409.*CREDENTIAL_KEY_CONFLICT",
            ):
                await client.register_ed25519_credential(
                    {
                        "userId": "user-3",
                        "publicKey": registered_public_key,
                    }
                )
            with pytest.raises(
                RuntimeError,
                match="A4P HTTP 409.*SIGNATURE_METHOD_NOT_ENABLED",
            ):
                await client.webauthn_registration_options({"userId": "user-1"})

            await http_server.start()
        finally:
            await http_server.stop()
            await http_server.stop()

    asyncio.run(run())


def test_real_http_server_returns_protocol_error_statuses() -> None:
    async def run() -> None:
        http_server = A4PHTTPServer(
            A4PServer(server_id="local://http-status-test"),
            host="127.0.0.1",
            port=0,
        )
        await http_server.start()
        try:
            get_response = await _raw_http_request(
                http_server.port,
                b"GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n",
            )
            invalid_json_response = await _raw_http_request(
                http_server.port,
                (
                    b"POST /a4p/v1/intent-authorizations/prepare HTTP/1.1\r\n"
                    b"Host: localhost\r\nContent-Length: 1\r\n\r\n{"
                ),
            )
            missing_response = await _raw_http_request(
                http_server.port,
                (
                    b"POST /missing HTTP/1.1\r\nHost: localhost\r\n"
                    b"Content-Length: 2\r\n\r\n{}"
                ),
            )

            assert get_response.startswith(b"HTTP/1.1 405 Method Not Allowed")
            assert invalid_json_response.startswith(b"HTTP/1.1 400 Bad Request")
            assert missing_response.startswith(b"HTTP/1.1 404 Not Found")
        finally:
            await http_server.stop()

    asyncio.run(run())
