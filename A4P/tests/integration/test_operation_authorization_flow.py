from __future__ import annotations

import asyncio
import copy

import pytest

from a4p import A4PClient
from a4p.http_server import A4PHTTPServer
from a4p.operation import mandate as operation_mandate
from tests.support import ExplicitEd25519A4PServer as A4PServer
from tests.support import sign_user_mandate


def _operation(note_id: str = "note-1") -> dict[str, object]:
    return {"action": "delete_note", "params": {"note_id": note_id}}


async def _prepare_signed(
    server: A4PServer,
) -> tuple[dict[str, object], dict[str, object]]:
    operation = _operation()
    challenge = await server.prepare_operation_authorization(
        {
            "agentId": "agent-1",
            "userId": "user-1",
            "operation": operation,
            "validitySeconds": 60,
        }
    )
    assert challenge.mandate is not None
    signed = sign_user_mandate(challenge.mandate)
    return operation, signed


def test_complete_requires_current_operation_and_matches_all_three_copies() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation, signed = await _prepare_signed(server)

        missing = await server.complete_operation_authorization(
            {"signedMandate": signed}
        )
        changed_current = await server.complete_operation_authorization(
            {
                "signedMandate": signed,
                "operation": _operation("note-2"),
            }
        )
        changed_mandate = copy.deepcopy(signed)
        changed_mandate["operation"] = _operation("note-2")
        changed_signed = await server.complete_operation_authorization(
            {
                "signedMandate": changed_mandate,
                "operation": operation,
            }
        )
        completed = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )

        assert missing.verificationResult is not None
        assert missing.verificationResult.code == "OPERATION_INVALID"
        assert changed_current.verificationResult is not None
        assert changed_current.verificationResult.code == "OPERATION_PENDING_MISMATCH"
        assert changed_signed.verificationResult is not None
        assert changed_signed.verificationResult.code == "MANDATE_PENDING_MISMATCH"
        assert completed.approved is True
        assert completed.operationId == signed["operationId"]

    asyncio.run(run())


def test_concurrent_complete_approves_and_executes_only_once() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation, signed = await _prepare_signed(server)
        executions = 0

        async def submit() -> str | None:
            nonlocal executions
            result = await server.complete_operation_authorization(
                {"signedMandate": signed, "operation": operation}
            )
            if result.approved:
                executions += 1
                return result.operationId
            return None

        results = await asyncio.gather(*(submit() for _ in range(5)))

        assert executions == 1
        assert results.count(signed["operationId"]) == 1
        assert results.count(None) == 4

    asyncio.run(run())


def test_business_failure_does_not_restore_consumed_authorization() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation, signed = await _prepare_signed(server)
        completed = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )
        assert completed.approved is True

        with pytest.raises(RuntimeError, match="business failed"):
            raise RuntimeError("business failed")

        replay = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )
        assert replay.verificationResult is not None
        assert replay.verificationResult.code == "AUTHORIZATION_NOT_PENDING"

    asyncio.run(run())


@pytest.mark.parametrize(
    ("payload", "reason"),
    [
        ({"userId": "user-1", "operation": _operation()}, "agentId missing"),
        ({"agentId": "agent-1", "operation": _operation()}, "userId missing"),
        (
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": _operation(),
                "validitySeconds": 0,
            },
            "validitySeconds must be a positive integer",
        ),
        (
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": _operation(),
                "validitySeconds": True,
            },
            "validitySeconds must be a positive integer",
        ),
        (
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": _operation(),
                "validitySeconds": "60",
            },
            "validitySeconds must be a positive integer",
        ),
    ],
)
def test_prepare_rejects_missing_identity_and_invalid_validity(payload, reason) -> None:  # noqa: ANN001
    challenge = asyncio.run(A4PServer().prepare_operation_authorization(payload))
    assert challenge.mandate is None
    assert challenge.rejectReason == reason


def test_expired_and_lost_pending_authorizations_fail_closed() -> None:
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        operation, signed = await _prepare_signed(server)
        operation_id = str(signed["operationId"])
        server._operation._pending[operation_id]["expireAtEpoch"] = 1

        expired = await server.complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )
        restarted = await A4PServer(
            server_id="local://test"
        ).complete_operation_authorization(
            {"signedMandate": signed, "operation": operation}
        )

        assert expired.verificationResult is not None
        assert expired.verificationResult.code == "AUTHORIZATION_NOT_PENDING"
        assert restarted.verificationResult is not None
        assert restarted.verificationResult.code == "AUTHORIZATION_NOT_PENDING"

    asyncio.run(run())


def test_legacy_direct_authorization_apis_are_removed() -> None:
    assert not hasattr(A4PClient, "request_intent_authorization")
    assert not hasattr(A4PServer, "authorize_intent")
    assert not hasattr(A4PClient, "request_operation_authorization")
    assert not hasattr(A4PClient, "webauthn_authorization_options")
    assert not hasattr(A4PServer, "authorize_operation")
    assert not hasattr(operation_mandate, "verify_operation_mandate")

    async def run() -> None:
        server = A4PHTTPServer(A4PServer())
        intent_status, intent_payload = await server._dispatch(
            "/a4p/v1/intent-authorizations",
            {},
        )
        operation_status, operation_payload = await server._dispatch(
            "/a4p/v1/operation-authorizations",
            {},
        )
        options_status, options_payload = await server._dispatch(
            "/a4p/v1/user-credentials/webauthn/authorization/options",
            {},
        )
        assert intent_status == 404
        assert intent_payload == {"error": "not_found"}
        assert operation_status == 404
        assert operation_payload == {"error": "not_found"}
        assert options_status == 404
        assert options_payload == {"error": "not_found"}

    asyncio.run(run())
