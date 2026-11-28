from __future__ import annotations

import time

import pytest

from a4p.credential_store import InMemoryCredentialStore

# The webauthn dependency is imported lazily, so these tests cover the challenge
# lifecycle without requiring the optional package.
from a4p.user_signature import webauthn as webauthn_module
from a4p.user_signature.webauthn import (
    MAX_PENDING_REGISTRATION_CHALLENGES,
    REGISTRATION_CHALLENGE_TTL_SECONDS,
    WebAuthnSignatureMethod,
)


class _Stub:
    def __init__(self, *args, **kwargs) -> None:
        pass


class _EnumStub:
    REQUIRED = "required"
    PREFERRED = "preferred"


@pytest.fixture()
def method(monkeypatch) -> WebAuthnSignatureMethod:
    def fake_load_api():
        return {
            "PublicKeyCredentialDescriptor": _Stub,
            "generate_registration_options": lambda **kwargs: object(),
            "AuthenticatorSelectionCriteria": _Stub,
            "ResidentKeyRequirement": _EnumStub,
            "UserVerificationRequirement": _EnumStub,
        }

    monkeypatch.setattr(webauthn_module, "_load_webauthn", fake_load_api)
    monkeypatch.setattr(
        webauthn_module, "_options_to_payload", lambda options: {"challenge": "stub"}
    )
    return WebAuthnSignatureMethod(InMemoryCredentialStore())


def test_pending_registration_challenges_are_bounded(method: WebAuthnSignatureMethod) -> None:
    """未 verify 的注册请求不能无限堆积（无鉴权端点，属内存耗尽面）。"""
    for _ in range(MAX_PENDING_REGISTRATION_CHALLENGES * 3):
        method.registration_options(user_id="user-1")

    assert len(method._registration_challenges) <= MAX_PENDING_REGISTRATION_CHALLENGES


def test_registration_challenge_carries_an_expiry(method: WebAuthnSignatureMethod) -> None:
    payload = method.registration_options(user_id="user-1")
    request_id = payload["registrationRequestId"]

    user_id, challenge, expires_at = method._registration_challenges[request_id]
    assert user_id == "user-1"
    assert challenge
    assert expires_at > time.time()
    assert expires_at <= time.time() + REGISTRATION_CHALLENGE_TTL_SECONDS


def test_expired_registration_challenge_is_rejected(method: WebAuthnSignatureMethod) -> None:
    payload = method.registration_options(user_id="user-1")
    request_id = payload["registrationRequestId"]

    user_id, challenge, _expires_at = method._registration_challenges[request_id]
    method._registration_challenges[request_id] = (user_id, challenge, time.time() - 1)

    with pytest.raises(ValueError, match="expired"):
        method.verify_registration(
            {"userId": "user-1", "registrationRequestId": request_id}
        )


def test_expired_challenges_are_pruned_on_new_registrations(
    method: WebAuthnSignatureMethod,
) -> None:
    stale = method.registration_options(user_id="user-1")["registrationRequestId"]
    user_id, challenge, _expires_at = method._registration_challenges[stale]
    method._registration_challenges[stale] = (user_id, challenge, time.time() - 1)

    method.registration_options(user_id="user-1")

    assert stale not in method._registration_challenges
