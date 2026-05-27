from __future__ import annotations

import asyncio
import logging

import pytest

from a4p import intent_mandate, operation_mandate
from a4p.http_server import A4PHTTPServer, a4p_http_port
from a4p.security import _WARNED_DEFAULT_KEYS


_ENV_NAMES = (
    "A4P_ENV",
    "APP_ENV",
    "ENV",
    "PYTHON_ENV",
    "INTENT_SERVER_SIGNING_KEY",
    "INTENT_USER_SIGNING_KEY",
    "OPERATION_SERVER_SIGNING_KEY",
    "OPERATION_USER_SIGNING_KEY",
    "A4P_SERVER_PORT",
)


@pytest.fixture(autouse=True)
def clean_env(monkeypatch: pytest.MonkeyPatch) -> None:
    for env_name in _ENV_NAMES:
        monkeypatch.delenv(env_name, raising=False)
    _WARNED_DEFAULT_KEYS.clear()


def test_default_server_signing_key_logs_high_risk_warning(caplog: pytest.LogCaptureFixture) -> None:
    caplog.set_level(logging.CRITICAL, logger="a4p.security")

    assert operation_mandate._server_signing_key()

    assert "HIGH RISK" in caplog.text
    assert "operation mandate server signing key" in caplog.text


def test_production_requires_explicit_server_signing_key(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("A4P_ENV", "production")

    with pytest.raises(RuntimeError, match="production mode"):
        intent_mandate._server_signing_key()


def test_production_rejects_configured_default_server_signing_key(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("A4P_ENV", "prod")
    monkeypatch.setenv("OPERATION_SERVER_SIGNING_KEY", operation_mandate._DEFAULT_SERVER_SIGNING_KEY)

    with pytest.raises(RuntimeError, match="built-in development signing key"):
        operation_mandate._server_signing_key()


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
