from __future__ import annotations

from pathlib import Path

import pytest

from a4p.security import _WARNED_DEFAULT_KEYS


_ENV_NAMES = (
    "A4P_ENV",
    "APP_ENV",
    "ENV",
    "PYTHON_ENV",
    "INTENT_SERVER_ED25519_PRIVATE_KEY",
    "INTENT_USER_ED25519_PRIVATE_KEY",
    "OPERATION_SERVER_ED25519_PRIVATE_KEY",
    "OPERATION_USER_ED25519_PRIVATE_KEY",
    "A4P_SERVER_PORT",
    "A4P_USAGE_DB_PATH",
)


@pytest.fixture(autouse=True)
def clean_env(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    for env_name in _ENV_NAMES:
        monkeypatch.delenv(env_name, raising=False)
    monkeypatch.setenv("A4P_USAGE_DB_PATH", str(tmp_path / "default-usage.sqlite3"))
    _WARNED_DEFAULT_KEYS.clear()
