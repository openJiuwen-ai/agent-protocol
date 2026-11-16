"""签名私钥 fail-closed 回归测试。

历史缺陷：未设置 ``*_ED25519_PRIVATE_KEY`` 时，SDK 回落到由源码中常量字符串
派生的 Ed25519 私钥（``sha256(label)``）。该私钥因此是公开可知的，而
``verify_*`` 又用本机这把键验签 —— 任何人都能离线伪造 intent/operation
mandate 与 token 并通过校验。
"""
from __future__ import annotations

import hashlib

import pytest

from a4p.security import (
    dev_signing_keys_allowed,
    ed25519_private_key_from_env,
)

LABEL = "a4p-intent-server-ed25519-dev-v1"
ENV_NAME = "INTENT_SERVER_ED25519_PRIVATE_KEY"
PURPOSE = "intent server signing"

_DEV_DERIVED_SEED = hashlib.sha256(LABEL.encode("utf-8")).digest()


def _clear_switches(monkeypatch: pytest.MonkeyPatch) -> None:
    for name in ("A4P_ENV", "APP_ENV", "ENV", "PYTHON_ENV", "A4P_ALLOW_DEV_KEYS", ENV_NAME):
        monkeypatch.delenv(name, raising=False)


def _key_bytes() -> bytes:
    from cryptography.hazmat.primitives import serialization

    key = ed25519_private_key_from_env(env_name=ENV_NAME, default_seed_label=LABEL, purpose=PURPOSE)
    return key.private_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PrivateFormat.Raw,
        encryption_algorithm=serialization.NoEncryption(),
    )


def test_unset_environment_refuses_the_built_in_key(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)

    assert dev_signing_keys_allowed() is False
    with pytest.raises(RuntimeError, match="refusing to fall back"):
        _key_bytes()


def test_explicit_opt_in_still_allows_local_development(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", "1")

    assert dev_signing_keys_allowed() is True
    assert _key_bytes() == _DEV_DERIVED_SEED


@pytest.mark.parametrize("env_value", ["dev", "development", "local", "test", "ci"])
def test_dev_environments_opt_in(monkeypatch: pytest.MonkeyPatch, env_value: str) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ENV", env_value)

    assert dev_signing_keys_allowed() is True
    assert _key_bytes() == _DEV_DERIVED_SEED


def test_production_rejects_even_with_opt_in(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ENV", "production")
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", "1")

    with pytest.raises(RuntimeError, match="production mode"):
        _key_bytes()


def test_explicit_secret_is_used_verbatim(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv(ENV_NAME, "hex:" + ("11" * 32))

    assert _key_bytes() != _DEV_DERIVED_SEED
