from __future__ import annotations

import asyncio
import copy

import pytest

from a4p import (
    approve_user_mandate,
    derive_user_authorization_challenge,
)
from a4p.user_signature.webauthn import (
    WebAuthnSignatureMethod,
    b64url_decode,
    b64url_encode,
)
from a4p.credential_store import InMemoryCredentialStore, UserCredentialRecord
from tests.support import ExplicitEd25519A4PServer as A4PServer
from tests.support import sign_user_mandate


def test_custom_operation_display_text_renderer_is_signed() -> None:
    async def run() -> None:
        operation = {"action": "delete_note", "params": {"note_id": "note-1"}}

        def note_display_text(mandate: dict[str, object]) -> str:
            operation = mandate["operation"]
            assert isinstance(operation, dict)
            params = operation["params"]
            assert isinstance(params, dict)
            return f"授权删除笔记：{params['note_id']}"

        server = A4PServer(
            server_id="local://test",
            operation_display_text_renderer=note_display_text,
        )
        prepared = await server.prepare_operation_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert prepared.mandate["displayText"] == "授权删除笔记：note-1"

        signed = sign_user_mandate(prepared.mandate)
        completed = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )

        assert completed.approved is True
        assert completed.operationId == signed["operationId"]

    asyncio.run(run())


def test_complete_operation_rejects_mandate_from_another_pending_request() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation = {"action": "delete_note", "params": {"note_id": "note-1"}}
        first = await server.prepare_operation_authorization(
            {
                "agentId": "agent-a",
                "userId": "user-a",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        second = await server.prepare_operation_authorization(
            {
                "agentId": "agent-b",
                "userId": "user-b",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        assert first.mandate is not None
        assert second.mandate is not None
        first_signed = sign_user_mandate(first.mandate)
        first_signed["operationId"] = second.mandate["operationId"]

        mismatched = await server.complete_operation_authorization(
            {"signedMandate": first_signed, "operation": operation}
        )
        second_signed = sign_user_mandate(second.mandate)
        completed = await server.complete_operation_authorization(
            {"signedMandate": second_signed, "operation": operation}
        )

        assert mismatched.approved is False
        assert mismatched.verificationResult is not None
        assert mismatched.verificationResult.code == "MANDATE_PENDING_MISMATCH"
        assert completed.approved is True
        assert completed.operationId == second_signed["operationId"]

    asyncio.run(run())


def test_prepare_complete_operation_rejects_replay() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation = {"action": "delete_note", "params": {"note_id": "note-1"}}
        prepared = await server.prepare_operation_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        signed = sign_user_mandate(prepared.mandate)

        completed = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )
        replay = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )

        assert completed.approved is True
        assert replay.approved is False
        assert replay.verificationResult is not None
        assert replay.verificationResult.code == "AUTHORIZATION_NOT_PENDING"

    asyncio.run(run())


def test_operation_no_signature_prepare_and_complete_returns_operation_id() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test", require_user_signature=False)
        operation = {"action": "delete_note", "params": {"note_id": "note-1"}}
        prepared = await server.prepare_operation_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert prepared.mandate["userAuthorization"] == {"required": False}
        approved = approve_user_mandate(prepared.mandate)

        completed = await server.complete_operation_authorization(
            {"signedMandate": approved, "operation": operation}
        )

        assert completed.approved is True
        assert completed.operationId == approved["operationId"]

    asyncio.run(run())


def test_operation_no_signature_approval_rejected_when_signature_required() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation = {"action": "delete_note", "params": {"note_id": "note-1"}}
        prepared = await server.prepare_operation_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert prepared.mandate["userAuthorization"]["required"] is True
        approved = approve_user_mandate(prepared.mandate)

        completed = await server.complete_operation_authorization(
            {"signedMandate": approved, "operation": operation}
        )

        assert completed.approved is False
        assert completed.verificationResult is not None
        assert completed.verificationResult.code == "MANDATE_SIGNATURE_INVALID"
        assert completed.rejectReason == "User signature missing"

    asyncio.run(run())


def test_operation_webauthn_user_signature_path_is_accepted(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = 0

    def fake_verify(self, context, signature):  # noqa: ANN001
        nonlocal calls
        calls += 1
        assert context.expected_user_id == "user-1"
        assert signature["credentialId"] == "cred-1"
        return True, ""

    monkeypatch.setattr(
        "a4p.user_signature.webauthn.WebAuthnSignatureMethod.verify",
        fake_verify,
    )
    monkeypatch.setattr(
        "a4p.user_signature.webauthn.WebAuthnSignatureMethod.signing_options",
        lambda self, *, user_id, mandate: {
            "signatureMethod": "webauthn",
            "methodOptions": {
                "challenge": b64url_encode(
                    derive_user_authorization_challenge(mandate)
                ),
                "rpId": "localhost",
                "allowCredentials": [
                    {"id": "cred-1", "type": "public-key"}
                ],
                "userVerification": "required",
            },
        },
    )
    store = InMemoryCredentialStore(
        [
            UserCredentialRecord(
                userId="user-1",
                credentialId="cred-1",
                signatureMethod="webauthn",
                publicKey={"format": "cose", "value": "public-key"},
                details={
                    "signCount": 0,
                    "rpId": "localhost",
                    "origin": "http://localhost:8970",
                },
            )
        ]
    )
    expected = {"action": "delete_note", "params": {"note_id": "note-1"}}

    async def run() -> None:
        server = A4PServer(
            server_id="local://test",
            user_signature_method=WebAuthnSignatureMethod(store),
        )
        prepared = await server.prepare_operation_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": expected,
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert b64url_decode(
            prepared.signingOptions["methodOptions"]["challenge"]
        ) == derive_user_authorization_challenge(prepared.mandate)
        assert (
            prepared.signingOptions["methodOptions"]["userVerification"]
            == "required"
        )
        signed = copy.deepcopy(prepared.mandate)
        signed["signatures"]["user"] = {
            "signatureMethod": "webauthn",
            "credentialId": "cred-1",
            "proof": {
                "assertion": {
                    "id": "cred-1",
                    "type": "public-key",
                    "response": {},
                }
            },
        }

        completed = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": expected}
        )

        assert completed.approved is True
        assert completed.operationId == signed["operationId"]
        assert calls == 1

    asyncio.run(run())
