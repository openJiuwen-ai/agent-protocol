from __future__ import annotations

import copy

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p import (
    MandateSecurityError,
    StaticA4PServerTrustStore,
    UserAuthorizationRequest,
    derive_user_authorization_challenge,
    user_authorization_challenge_base64url,
    verify_local_user_authorization_request,
)
from a4p.user_signature.webauthn import (
    WebAuthnSignatureMethod,
    b64url_decode,
    b64url_encode,
)
from a4p.credential_store import InMemoryCredentialStore, UserCredentialRecord
from a4p.intent.mandate import create_intent_mandate
from a4p.operation.mandate import create_operation_mandate, sign_server_mandate
from a4p.security import ed25519_public_key_to_base64url
from tests.support import ExplicitEd25519A4PServer as A4PServer


def _prepared_operation() -> tuple[A4PServer, dict]:
    server = A4PServer(server_id="local://security-test")
    mandate = create_operation_mandate(
        operation={
            "action": "transfer",
            "params": {"amount": 10, "currency": "CNY"},
        },
        server_url=server.server_id,
        agent_id="agent-1",
        validity_seconds=60,
        user_signature_method="webauthn",
        user_signature_method_policy={"userVerification": "required"},
    )
    return server, mandate


def _local_request(mandate: dict, *, signing_options: dict | None = None):
    return UserAuthorizationRequest(
        mandate=mandate,
        signingOptions=signing_options or {},
    )


def test_authorization_ids_are_32_byte_random_values() -> None:
    """创建 intent 和 operation mandate 时，应生成可解码为 32 字节且彼此不同的随机授权 ID。"""
    intent_ids = {
        create_intent_mandate(
            server="local://security-test",
            agent_id="agent-1",
            actions=[{"name": "read", "params": {}}],
            user_signature_method="ed25519",
        )["mandateId"]
        for _ in range(100)
    }
    operation_ids = {
        create_operation_mandate(
            operation={"action": "write", "params": {}},
            server_url="local://security-test",
            user_signature_method="ed25519",
        )["operationId"]
        for _ in range(100)
    }

    assert len(intent_ids) == 100
    assert len(operation_ids) == 100
    assert all(identifier.startswith("mdt_") for identifier in intent_ids)
    assert all(identifier.startswith("op_") for identifier in operation_ids)
    assert all(
        len(b64url_decode(identifier.removeprefix("mdt_"))) == 32
        for identifier in intent_ids
    )
    assert all(
        len(b64url_decode(identifier.removeprefix("op_"))) == 32
        for identifier in operation_ids
    )


def test_challenge_is_deterministic_and_binds_server_signed_mandate() -> None:
    """同一服务端已签名 mandate 应派生稳定 challenge，而签名或内容变化应产生不同 challenge。"""
    _server, mandate = _prepared_operation()
    challenge = derive_user_authorization_challenge(mandate)

    assert len(challenge) == 32
    assert derive_user_authorization_challenge(mandate) == challenge

    for path, value in (
        (("operation", "params", "amount"), 11),
        (("subject", "id"), "agent:other"),
        (("validTime", "until"), "2099-01-01T00:00:00Z"),
        (("displayText",), "Approve something else"),
        (("operationId",), "op_changed"),
        (("signatures", "server", "signature"), b64url_encode(b"x" * 64)),
    ):
        changed = copy.deepcopy(mandate)
        target = changed
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        assert derive_user_authorization_challenge(changed) != challenge

    with_user_signature = copy.deepcopy(mandate)
    with_user_signature["signatures"]["user"] = {
        "signatureMethod": "webauthn",
        "credentialId": "credential-1",
        "proof": {"assertion": {"signature": "first"}},
    }
    assert derive_user_authorization_challenge(with_user_signature) == challenge
    with_user_signature["signatures"]["user"]["proof"]["assertion"]["signature"] = "second"
    assert derive_user_authorization_challenge(with_user_signature) == challenge


def test_local_authorizer_verifies_server_and_overwrites_untrusted_options() -> None:
    """本地授权器处理可信服务端 mandate 时，应验证签名并以可信签名选项覆盖外部输入。"""
    server, mandate = _prepared_operation()
    trust_store = StaticA4PServerTrustStore(server.server_trust_config())

    hardened = verify_local_user_authorization_request(
        _local_request(
            mandate,
            signing_options={
                "signatureMethod": "webauthn",
                "methodOptions": {
                    "challenge": b64url_encode(b"attacker-controlled"),
                    "userVerification": "preferred",
                    "rpId": "localhost",
                },
            },
        ),
        trust_store=trust_store,
        expected_signature_method="webauthn",
    )

    assert hardened["methodOptions"]["challenge"] == user_authorization_challenge_base64url(mandate)
    assert hardened["methodOptions"]["userVerification"] == "required"
    assert hardened["methodOptions"]["rpId"] == "localhost"


def test_local_authorizer_rejects_untrusted_key_invalid_signature_and_expiry() -> None:
    """本地授权器遇到不可信密钥、无效签名或过期 mandate 时，应全部拒绝。"""
    server, mandate = _prepared_operation()
    trust_store = StaticA4PServerTrustStore(server.server_trust_config())

    untrusted = copy.deepcopy(mandate)
    untrusted["signatures"]["server"]["keyId"] = "server#unknown"
    with pytest.raises(MandateSecurityError) as untrusted_error:
        verify_local_user_authorization_request(
            _local_request(untrusted),
            trust_store=trust_store,
        )
    assert untrusted_error.value.code == "SERVER_KEY_UNTRUSTED"

    tampered = copy.deepcopy(mandate)
    tampered["displayText"] = "Approve an attacker-controlled operation"
    with pytest.raises(MandateSecurityError) as signature_error:
        verify_local_user_authorization_request(
            _local_request(tampered),
            trust_store=trust_store,
        )
    assert signature_error.value.code == "SERVER_SIGNATURE_INVALID"

    expired = copy.deepcopy(mandate)
    expired["validTime"]["until"] = "2000-01-01T00:00:00Z"
    expired = sign_server_mandate(expired)
    with pytest.raises(MandateSecurityError) as expired_error:
        verify_local_user_authorization_request(
            _local_request(expired),
            trust_store=trust_store,
        )
    assert expired_error.value.code == "CHALLENGE_BINDING_INVALID"

    wrong_public_key = ed25519_public_key_to_base64url(
        Ed25519PrivateKey.generate().public_key()
    )
    key_id = mandate["signatures"]["server"]["keyId"]
    wrong_trust = StaticA4PServerTrustStore(
        {
            mandate["server"]: {
                key_id: {"alg": "EdDSA", "publicKey": wrong_public_key},
            }
        }
    )
    with pytest.raises(MandateSecurityError) as wrong_key_error:
        verify_local_user_authorization_request(
            _local_request(mandate),
            trust_store=wrong_trust,
        )
    assert wrong_key_error.value.code == "SERVER_SIGNATURE_INVALID"


def test_mandate_user_authorization_has_no_independent_challenge_fields() -> None:
    """mandate 的 userAuthorization 中不应包含可独立篡改的 challenge 或 challengeMethod 字段。"""
    _server, mandate = _prepared_operation()
    assert "challenge" not in mandate["userAuthorization"]
    assert "challengeMethod" not in mandate["userAuthorization"]
    assert "challengeNonce" not in mandate["userAuthorization"]


def test_webauthn_uses_raw_derived_challenge_bytes(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """生成 WebAuthn 认证选项时，应将 mandate 派生 challenge 的原始字节传给 WebAuthn 库。"""
    captured: dict = {}

    class _Descriptor:
        def __init__(self, **kwargs):  # noqa: ANN003
            self.kwargs = kwargs

    class _Requirement:
        REQUIRED = "required"

    def generate_authentication_options(**kwargs):  # noqa: ANN003
        captured.update(kwargs)
        return kwargs

    monkeypatch.setattr(
        "a4p.user_signature.webauthn._load_webauthn",
        lambda: {
            "PublicKeyCredentialDescriptor": _Descriptor,
            "UserVerificationRequirement": _Requirement,
            "generate_authentication_options": generate_authentication_options,
            "options_to_json": lambda options: "{}",
        },
    )
    store = InMemoryCredentialStore(
        [
            UserCredentialRecord(
                userId="user-1",
                credentialId=b64url_encode(b"credential-id"),
                signatureMethod="webauthn",
                publicKey={"format": "cose", "value": b64url_encode(b"public-key")},
                details={
                    "signCount": 0,
                    "rpId": "localhost",
                    "origin": "http://localhost:8970",
                },
            )
        ]
    )
    _server, mandate = _prepared_operation()
    options = WebAuthnSignatureMethod(store).signing_options(
        user_id="user-1",
        mandate=mandate,
    )

    assert captured["challenge"] == derive_user_authorization_challenge(mandate)
    assert options["signatureMethod"] == "webauthn"
