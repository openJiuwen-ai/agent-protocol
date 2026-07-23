from __future__ import annotations

import json
import logging
from dataclasses import is_dataclass

import pytest

from a4p import (
    IntentMandate,
    IntentToken,
    OperationMandate,
)
from a4p.intent.signing import intent_server_signing_key
from a4p.operation.signing import (
    OPERATION_SERVER_PRIVATE_KEY_LABEL,
    operation_server_signing_key,
)
from a4p.types import to_payload


def test_wire_types_are_plain_json_serializable_dicts() -> None:
    intent_mandate = IntentMandate(
        type="a4p/v1/intent-mandate",
        mandateId="mdt-1",
        server="local://test",
        subject={},
        intent={},
        validTime={},
        userAuthorization={},
        displayText="test intent",
        signatures={},
    )
    intent_token = IntentToken(
        type="a4p/v1/intent-token",
        tokenId="token-1",
        mandateId="mdt-1",
        subject={},
        user={},
        intent={},
        issuedAt="2026-07-21T00:00:00Z",
        expireAt="2026-07-21T01:00:00Z",
        nonce="nonce-1",
        signature="signature-1",
        alg="EdDSA",
        keyId="key-1",
    )
    operation_mandate = OperationMandate(
        type="a4p/v1/operation-mandate",
        operationId="op-1",
        server="local://test",
        subject={},
        operation={},
        validTime={},
        userAuthorization={},
        displayText="test operation",
        signatures={},
    )

    for wire_object in (intent_mandate, intent_token, operation_mandate):
        assert type(wire_object) is dict
        assert not is_dataclass(wire_object)
        assert to_payload(wire_object) == wire_object
        assert json.loads(json.dumps(wire_object)) == wire_object


def test_default_server_signing_key_logs_high_risk_warning(
    caplog: pytest.LogCaptureFixture,
) -> None:
    caplog.set_level(logging.CRITICAL, logger="a4p.security")

    assert operation_server_signing_key()

    assert "HIGH RISK" in caplog.text
    assert "operation mandate server Ed25519 signing key" in caplog.text


def test_production_requires_explicit_server_signing_key(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("A4P_ENV", "production")

    with pytest.raises(RuntimeError, match="production mode"):
        intent_server_signing_key()


def test_production_rejects_configured_default_server_signing_key(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("A4P_ENV", "prod")
    monkeypatch.setenv(
        "OPERATION_SERVER_ED25519_PRIVATE_KEY",
        OPERATION_SERVER_PRIVATE_KEY_LABEL,
    )

    with pytest.raises(RuntimeError, match="built-in development Ed25519 signing key"):
        operation_server_signing_key()
