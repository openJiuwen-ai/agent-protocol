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


@pytest.mark.parametrize("falsy", ["0", "false", "no", "off", "2", "", " "])
def test_opt_in_falsy_values_do_not_enable_the_built_in_key(
    monkeypatch: pytest.MonkeyPatch, falsy: str
) -> None:
    """``A4P_ALLOW_DEV_KEYS=0`` 必须是关闭：真值判断会让公开私钥静默复活。"""
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", falsy)

    assert dev_signing_keys_allowed() is False
    with pytest.raises(RuntimeError, match="refusing to fall back"):
        _key_bytes()


@pytest.mark.parametrize("truthy", ["1", "true", "TRUE", " yes ", "on"])
def test_opt_in_truthy_values_enable_the_built_in_key(
    monkeypatch: pytest.MonkeyPatch, truthy: str
) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", truthy)

    assert _key_bytes() == _DEV_DERIVED_SEED


def test_production_rejects_even_with_opt_in(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ENV", "production")
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", "1")

    # 语义独立：不依赖调用方先查 is_production_environment()。
    assert dev_signing_keys_allowed() is False
    with pytest.raises(RuntimeError, match="production mode"):
        _key_bytes()


def test_explicit_secret_is_used_verbatim(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv(ENV_NAME, "hex:" + ("11" * 32))

    assert _key_bytes() != _DEV_DERIVED_SEED


@pytest.mark.parametrize("blank", ["", " ", "   ", "\t\n"])
def test_blank_explicit_key_is_a_config_error_not_an_opt_in(
    monkeypatch: pytest.MonkeyPatch, blank: str
) -> None:
    """``KEY=`` 这类空占位不能被当成「未设置」而静默启用公开私钥。"""
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ENV", "dev")
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", "1")
    monkeypatch.setenv(ENV_NAME, blank)

    with pytest.raises(RuntimeError, match="is set but empty"):
        _key_bytes()


def test_blank_explicit_key_is_rejected_in_production(monkeypatch: pytest.MonkeyPatch) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("A4P_ENV", "production")
    monkeypatch.setenv(ENV_NAME, "")

    with pytest.raises(RuntimeError, match="is set but empty"):
        _key_bytes()


@pytest.mark.parametrize("env_name", ["ENV", "PYTHON_ENV"])
@pytest.mark.parametrize("env_value", ["dev", "development", "local", "test", "ci"])
def test_generic_env_vars_do_not_enable_the_built_in_key(
    monkeypatch: pytest.MonkeyPatch, env_name: str, env_value: str
) -> None:
    """``ENV`` / ``PYTHON_ENV`` 常被构建系统无关地设置，不能成为隐式开启面。"""
    _clear_switches(monkeypatch)
    monkeypatch.setenv(env_name, env_value)

    assert dev_signing_keys_allowed() is False
    with pytest.raises(RuntimeError, match="refusing to fall back"):
        _key_bytes()


@pytest.mark.parametrize("env_name", ["ENV", "PYTHON_ENV"])
def test_generic_production_env_vars_still_deny_the_built_in_key(
    monkeypatch: pytest.MonkeyPatch, env_name: str
) -> None:
    """收紧只作用于「允许」方向：通用变量报生产时依旧否决。"""
    _clear_switches(monkeypatch)
    monkeypatch.setenv(env_name, "production")
    monkeypatch.setenv("A4P_ALLOW_DEV_KEYS", "1")

    assert dev_signing_keys_allowed() is False
    with pytest.raises(RuntimeError, match="production mode"):
        _key_bytes()


@pytest.mark.parametrize("env_value", ["dev", "test", "ci"])
def test_app_env_also_opts_in(monkeypatch: pytest.MonkeyPatch, env_value: str) -> None:
    _clear_switches(monkeypatch)
    monkeypatch.setenv("APP_ENV", env_value)

    assert dev_signing_keys_allowed() is True
    assert _key_bytes() == _DEV_DERIVED_SEED
