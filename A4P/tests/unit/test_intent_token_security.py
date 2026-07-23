from __future__ import annotations

import asyncio
import copy
from typing import Any

import pytest

from a4p.intent import token as intent_token
from a4p.intent.mandate import create_intent_mandate
from tests.support import ExplicitEd25519A4PServer as A4PServer


def _valid_token() -> dict[str, Any]:
    mandate = create_intent_mandate(
        server="local://token-test",
        agent_id="agent-1",
        agent_public_key={
            "kty": "OKP",
            "kid": "agent-key-1",
            "crv": "Ed25519",
            "x": "test-public-key",
        },
        actions=[{"name": "delete_note", "params": {"note_id": "*"}}],
        validity_seconds=3600,
        user_signature_method="ed25519",
    )
    return intent_token.issue_intent_token(
        mandate,
        user_id="user-1",
        verified_mandate=True,
    )


@pytest.mark.parametrize(
    ("field", "value", "expected_reason"),
    [
        ("type", "a4p/v0/intent-token", "Invalid token type"),
        ("alg", "LegacySymmetric", "Token alg mismatch"),
        ("signature", "", "Token signature missing"),
        ("mandateId", "", "Token mandateId missing"),
        ("keyId", "server#wrong-key", "Token keyId mismatch"),
    ],
)
def test_intent_token_rejects_invalid_signature_metadata(
    field: str,
    value: str,
    expected_reason: str,
) -> None:
    token = _valid_token()
    token[field] = value

    valid, reason = intent_token.verify_intent_token(
        token,
        action="delete_note",
        params={"note_id": "note-1"},
    )

    assert valid is False
    assert expected_reason in reason


def test_intent_token_rejects_signed_scope_tampering() -> None:
    token = _valid_token()
    token["intent"]["actions"][0]["params"]["note_id"] = "attacker-note"

    valid, reason = intent_token.verify_intent_token(
        token,
        action="delete_note",
        params={"note_id": "attacker-note"},
    )

    assert valid is False
    assert reason == "Token signature invalid"


@pytest.mark.parametrize(
    ("expire_at", "expected_reason"),
    [
        ("", "Token expireAt missing"),
        ("not-a-timestamp", "Token expireAt format invalid"),
        ("2000-01-01T00:00:00Z", "Token expired"),
    ],
)
def test_intent_token_rejects_invalid_expiration_after_signature_verification(
    monkeypatch: pytest.MonkeyPatch,
    expire_at: str,
    expected_reason: str,
) -> None:
    monkeypatch.setattr("a4p.intent.token.ed25519_verify_text", lambda *args: True)
    token = _valid_token()
    token["expireAt"] = expire_at

    valid, reason = intent_token.verify_intent_token(
        token,
        action="delete_note",
        params={"note_id": "note-1"},
    )

    assert valid is False
    assert reason == expected_reason


@pytest.mark.parametrize(
    ("verification_kwargs", "expected_reason"),
    [
        (
            {"expected_agent_id": "agent-2"},
            "Token subject mismatch: expected 'agent-2', got 'agent:agent-1'",
        ),
        (
            {"expected_agent_key_id": "agent-key-2"},
            "Token agent key mismatch: expected 'agent-key-2', got 'agent-key-1'",
        ),
        (
            {"expected_user_id": "user-2"},
            "Token user mismatch: expected 'user-2', got 'user-1'",
        ),
    ],
)
def test_intent_token_rejects_identity_binding_mismatches(
    verification_kwargs: dict[str, str],
    expected_reason: str,
) -> None:
    valid, reason = intent_token.verify_intent_token(
        _valid_token(),
        action="delete_note",
        params={"note_id": "note-1"},
        **verification_kwargs,
    )

    assert valid is False
    assert reason == expected_reason


def test_intent_token_rejects_non_canonical_json_values() -> None:
    token = _valid_token()
    token["intent"]["actions"][0]["params"]["amount"] = float("nan")

    valid, reason = intent_token.verify_intent_token(
        token,
        action="delete_note",
        params={"note_id": "note-1"},
    )

    assert valid is False
    assert "Out of range float values" in reason


@pytest.mark.parametrize(
    ("mutation", "expected", "expected_code"),
    [
        ({"type": "a4p/v0/intent-token"}, {}, "TOKEN_INVALID"),
        ({"signature": ""}, {}, "TOKEN_SIGNATURE_INVALID"),
        (
            {"expireAt": "2000-01-01T00:00:00Z"},
            {},
            "TOKEN_EXPIRED",
        ),
        ({}, {"userId": "user-2"}, "TOKEN_SCOPE_MISMATCH"),
    ],
)
def test_intent_token_service_returns_stable_fail_closed_codes(
    monkeypatch: pytest.MonkeyPatch,
    mutation: dict[str, Any],
    expected: dict[str, Any],
    expected_code: str,
) -> None:
    monkeypatch.setattr("a4p.intent.token.ed25519_verify_text", lambda *args: True)
    token = copy.deepcopy(_valid_token())
    token.update(mutation)

    response = asyncio.run(
        A4PServer(server_id="local://token-test").verify_intent_token(
            {
                "token": token,
                "expected": {
                    "action": "delete_note",
                    "params": {"note_id": "note-1"},
                    **expected,
                },
            }
        )
    )

    assert response.valid is False
    assert response.code == expected_code
