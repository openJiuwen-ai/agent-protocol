from __future__ import annotations

import asyncio
import copy
import json
from dataclasses import replace
from types import SimpleNamespace

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p import (
    A4PServer,
    CredentialKeyConflictError,
    InMemoryCredentialStore,
    SignatureMethodNotEnabledError,
    approve_user_mandate,
    sign_user_mandate_with_signer,
)
from a4p.user_signature.webauthn import (
    WebAuthnSignatureMethod,
    WebAuthnUserSigner,
    _load_webauthn,
    b64url_encode,
)
from a4p.user_signature.ed25519 import (
    Ed25519UserSigner,
    RegisteredEd25519Method,
    ed25519_public_jwk,
)
from a4p.credential_store import UserCredentialRecord
from a4p.mandate_security import derive_user_authorization_challenge
from a4p.operation.mandate import (
    create_operation_mandate,
    operation_user_signature_context,
)


def _ed25519_server() -> tuple[
    A4PServer,
    RegisteredEd25519Method,
    Ed25519PrivateKey,
    Ed25519UserSigner,
]:
    private_key = Ed25519PrivateKey.generate()
    method = RegisteredEd25519Method(InMemoryCredentialStore())
    server = A4PServer(
        server_id="local://ed25519-test",
        user_signature_method=method,
    )
    registration = server.register_ed25519_credential(
        {
            "userId": "user-1",
            "publicKey": ed25519_public_jwk(private_key),
            "metadata": {"label": "test key"},
        }
    )
    signer = Ed25519UserSigner(
        credential_id=registration["credential"]["credentialId"],
        private_key=private_key,
    )
    return server, method, private_key, signer


def _operation_request(user_id: str = "user-1") -> dict:
    return {
        "agentId": "agent-1",
        "userId": user_id,
        "operation": {
            "action": "delete_note",
            "params": {"note_id": "note-1"},
        },
        "validitySeconds": 60,
    }


def test_server_requires_an_explicit_method_for_signed_mode() -> None:
    """服务端要求用户签名但未配置签名方法时，应在初始化阶段抛出错误。"""
    with pytest.raises(ValueError, match="user_signature_method is required"):
        A4PServer()

    server = A4PServer(require_user_signature=False)
    prepared = asyncio.run(server.prepare_operation_authorization(_operation_request()))

    assert prepared.mandate is not None
    assert prepared.mandate["userAuthorization"] == {"required": False}
    assert prepared.mandate["signatures"]["user"] == {}
    assert prepared.signingOptions == {}


def test_webauthn_dependency_exports_are_loadable() -> None:
    """加载 WebAuthn 可选依赖时，应获得注册、认证和验证流程所需的全部导出对象。"""
    _load_webauthn.cache_clear()
    exports = _load_webauthn()

    assert "generate_authentication_options" in exports
    assert "verify_registration_response" in exports


def test_server_rejects_invalid_signature_method_identifier() -> None:
    """签名方法标识符包含大写等非规范格式时，服务端应拒绝配置。"""
    method = SimpleNamespace(signature_method="WebAuthn")
    with pytest.raises(ValueError, match="lowercase identifier"):
        A4PServer(user_signature_method=method)  # type: ignore[arg-type]


@pytest.mark.parametrize(
    ("public_key_mutation", "error"),
    [
        ({"kty": "EC"}, "kty"),
        ({"crv": "X25519"}, "crv"),
        ({"alg": "ES256"}, "alg"),
        ({"alg": ""}, "alg"),
        ({"x": "AA"}, "32 bytes"),
        ({"x": "!" * 43}, "base64url"),
        ({"d": "private"}, "private key"),
    ],
)
def test_ed25519_registration_rejects_invalid_jwk(
    public_key_mutation: dict[str, str],
    error: str,
) -> None:
    """注册 Ed25519 凭据时提供类型、曲线或公钥值非法的 JWK，应拒绝注册。"""
    method = RegisteredEd25519Method(InMemoryCredentialStore())
    public_key = ed25519_public_jwk(Ed25519PrivateKey.generate())
    public_key.update(public_key_mutation)

    with pytest.raises(ValueError, match=error):
        method.register({"userId": "user-1", "publicKey": public_key})


def test_ed25519_registration_is_random_idempotent_and_conflict_safe() -> None:
    """注册 Ed25519 凭据时，应生成随机 ID、对同用户同密钥保持幂等并拒绝跨用户密钥冲突。"""
    method = RegisteredEd25519Method(InMemoryCredentialStore())
    public_key = ed25519_public_jwk(Ed25519PrivateKey.generate())
    first = method.register(
        {
            "userId": "user-1",
            "publicKey": public_key,
            "metadata": {"label": "primary"},
        }
    )
    repeated = method.register(
        {
            "userId": "user-1",
            "publicKey": public_key,
            "metadata": {"label": "ignored on idempotent retry"},
        }
    )

    assert first["created"] is True
    assert first["credential"]["credentialId"].startswith("cred_")
    assert first["credential"]["credentialId"] != "user-1"
    assert repeated["created"] is False
    assert repeated["credential"] == first["credential"]

    with pytest.raises(CredentialKeyConflictError) as conflict:
        method.register({"userId": "user-2", "publicKey": public_key})
    assert conflict.value.code == "CREDENTIAL_KEY_CONFLICT"
    assert conflict.value.http_status == 409


@pytest.mark.parametrize(
    ("request_payload", "error"),
    [
        ({"userId": "", "publicKey": {}}, "userId missing"),
        ({"userId": "user-1", "publicKey": None}, "JWK object"),
        (
            {
                "userId": "user-1",
                "publicKey": ed25519_public_jwk(Ed25519PrivateKey.generate()),
                "metadata": [],
            },
            "metadata must be an object",
        ),
    ],
)
def test_ed25519_registration_rejects_invalid_request_shape(
    request_payload: dict,
    error: str,
) -> None:
    """Ed25519 注册请求缺少用户或公钥字段时，应返回对应的参数错误。"""
    with pytest.raises(ValueError, match=error):
        RegisteredEd25519Method(InMemoryCredentialStore()).register(request_payload)


def test_ed25519_method_rejects_malformed_registered_proofs() -> None:
    """Ed25519 已注册凭据提交缺失字段或格式错误的签名 proof 时，应拒绝验证。"""
    server, method, _private_key, signer = _ed25519_server()
    prepared = asyncio.run(server.prepare_operation_authorization(_operation_request()))
    assert prepared.mandate is not None
    context = operation_user_signature_context(
        prepared.mandate,
        expected_user_id="user-1",
    )

    assert method.verify(context, {}) == (False, "User credentialId missing")
    signature = {
        "signatureMethod": "ed25519",
        "credentialId": signer.credential_id,
    }
    assert method.verify(context, signature) == (
        False,
        "User signature proof missing",
    )
    signature["proof"] = {"alg": "EdDSA", "signature": ""}
    assert method.verify(context, signature) == (False, "User signature missing")

    record = method.credential_store.get(signer.credential_id)
    assert record is not None
    method.credential_store.save(replace(record, signatureMethod="webauthn"))
    assert method.verify(context, signature) == (
        False,
        "User credential signature method mismatch",
    )
    method.credential_store.save(
        replace(record, publicKey={**record.publicKey, "x": "bad"})
    )
    signature["proof"] = {"alg": "EdDSA", "signature": "AAAA"}
    valid, reason = method.verify(context, signature)
    assert valid is False
    assert "Registered Ed25519 public key invalid" in reason

    with pytest.raises(ValueError, match="credential_id missing"):
        Ed25519UserSigner(
            credential_id="",
            private_key=Ed25519PrivateKey.generate(),
        )


def test_registration_endpoint_must_match_configured_method() -> None:
    """调用与服务端当前签名方法不匹配的凭据注册接口时，应返回签名方法未启用错误。"""
    ed_server, _method, _key, _signer = _ed25519_server()
    with pytest.raises(SignatureMethodNotEnabledError) as webauthn_error:
        ed_server.webauthn_registration_options({"userId": "user-1"})
    assert webauthn_error.value.code == "SIGNATURE_METHOD_NOT_ENABLED"

    webauthn_server = A4PServer(
        user_signature_method=WebAuthnSignatureMethod(InMemoryCredentialStore())
    )
    with pytest.raises(SignatureMethodNotEnabledError):
        webauthn_server.register_ed25519_credential(
            {
                "userId": "user-1",
                "publicKey": ed25519_public_jwk(Ed25519PrivateKey.generate()),
            }
        )


def test_prepare_fails_stably_without_registered_credential_and_adds_no_pending() -> None:
    """用户没有已注册凭据时，准备授权应稳定失败且不得新增待处理记录。"""
    method = RegisteredEd25519Method(InMemoryCredentialStore())
    server = A4PServer(user_signature_method=method)

    prepared = asyncio.run(server.prepare_operation_authorization(_operation_request()))

    assert prepared.mandate is None
    assert prepared.verificationResult is not None
    assert prepared.verificationResult.code == "USER_CREDENTIAL_NOT_REGISTERED"
    assert server._operation._pending == {}


def test_ed25519_operation_and_intent_use_common_envelope() -> None:
    """Ed25519 对 operation 和 intent 授权签名时，应使用相同的用户签名信封结构并通过验证。"""
    async def run() -> None:
        server, _method, _key, signer = _ed25519_server()
        operation_request = _operation_request()
        prepared = await server.prepare_operation_authorization(operation_request)
        assert prepared.mandate is not None
        assert prepared.mandate["userAuthorization"] == {
            "required": True,
            "signatureMethod": "ed25519",
            "methodPolicy": {},
        }
        assert prepared.signingOptions == {
            "signatureMethod": "ed25519",
            "methodOptions": {
                "allowedCredentialIds": [signer.credential_id],
            },
        }
        signed = sign_user_mandate_with_signer(
            prepared.mandate,
            user_signer=signer,
        )
        assert signed["signatures"]["user"]["proof"]["alg"] == "EdDSA"
        assert set(signed["signatures"]["user"]) == {
            "signatureMethod",
            "credentialId",
            "proof",
        }
        completed = await server.complete_operation_authorization(
            {
                "signedMandate": signed,
                "operation": operation_request["operation"],
            }
        )
        assert completed.approved is True

        intent_prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "read_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert intent_prepared.mandate is not None
        intent_signed = sign_user_mandate_with_signer(
            intent_prepared.mandate,
            user_signer=signer,
        )
        intent_completed = await server.complete_intent_authorization(
            {"signedMandate": intent_signed}
        )
        assert intent_completed.approved is True
        assert intent_completed.intentToken is not None

    asyncio.run(run())


@pytest.mark.parametrize(
    ("mutation", "reason"),
    [
        (
            lambda signature: signature.update({"signatureMethod": "webauthn"}),
            "method mismatch",
        ),
        (
            lambda signature: signature["proof"].update({"alg": "ES256"}),
            "alg must be 'EdDSA'",
        ),
        (
            lambda signature: signature.update({"credentialId": "cred_unknown"}),
            "not registered",
        ),
        (
            lambda signature: signature["proof"].update({"signature": "AAAA"}),
            "signature invalid",
        ),
    ],
)
def test_ed25519_authorization_rejects_wrong_proof_fields(
    mutation,
    reason: str,
) -> None:
    """Ed25519 授权签名的方法、算法、凭据 ID 或签名值被替换时，应拒绝完成授权。"""
    async def run() -> None:
        server, _method, _key, signer = _ed25519_server()
        prepared = await server.prepare_operation_authorization(_operation_request())
        assert prepared.mandate is not None
        signed = sign_user_mandate_with_signer(
            prepared.mandate,
            user_signer=signer,
        )
        mutation(signed["signatures"]["user"])
        completed = await server.complete_operation_authorization(
            {
                "signedMandate": signed,
                "operation": _operation_request()["operation"],
            }
        )
        assert completed.approved is False
        assert reason in (completed.rejectReason or "")

    asyncio.run(run())


def test_ed25519_authorization_rejects_wrong_user_and_mandate_tampering() -> None:
    """使用其他用户凭据签名或篡改 mandate 后提交 Ed25519 授权时，应因身份或待处理内容不匹配而拒绝。"""
    async def run() -> None:
        server, method, _key, _signer = _ed25519_server()
        attacker_key = Ed25519PrivateKey.generate()
        attacker_registration = method.register(
            {
                "userId": "user-2",
                "publicKey": ed25519_public_jwk(attacker_key),
            }
        )
        attacker_signer = Ed25519UserSigner(
            credential_id=attacker_registration["credential"]["credentialId"],
            private_key=attacker_key,
        )
        prepared = await server.prepare_operation_authorization(_operation_request())
        assert prepared.mandate is not None
        wrong_user = sign_user_mandate_with_signer(
            prepared.mandate,
            user_signer=attacker_signer,
        )
        rejected = await server.complete_operation_authorization(
            {
                "signedMandate": wrong_user,
                "operation": _operation_request()["operation"],
            }
        )
        assert rejected.approved is False
        assert "user mismatch" in (rejected.rejectReason or "")

        tampered = copy.deepcopy(prepared.mandate)
        tampered["displayText"] = "attacker-controlled"
        tampered = sign_user_mandate_with_signer(
            tampered,
            user_signer=attacker_signer,
        )
        rejected_tamper = await server.complete_operation_authorization(
            {
                "signedMandate": tampered,
                "operation": _operation_request()["operation"],
            }
        )
        assert rejected_tamper.approved is False
        assert rejected_tamper.verificationResult is not None
        assert rejected_tamper.verificationResult.code == "MANDATE_PENDING_MISMATCH"

    asyncio.run(run())


def test_ed25519_signing_options_include_multiple_credentials() -> None:
    """同一用户注册多个 Ed25519 凭据时，签名选项应列出全部允许的凭据 ID。"""
    method = RegisteredEd25519Method(InMemoryCredentialStore())
    server = A4PServer(user_signature_method=method)
    credential_ids = []
    for _ in range(2):
        registration = method.register(
            {
                "userId": "user-1",
                "publicKey": ed25519_public_jwk(Ed25519PrivateKey.generate()),
            }
        )
        credential_ids.append(registration["credential"]["credentialId"])

    prepared = asyncio.run(server.prepare_operation_authorization(_operation_request()))

    assert prepared.signingOptions["methodOptions"]["allowedCredentialIds"] == credential_ids


def _mock_webauthn(monkeypatch: pytest.MonkeyPatch) -> dict:
    captured: dict = {}

    class Descriptor:
        def __init__(self, *, id: bytes) -> None:
            self.id = id

    class Requirement:
        REQUIRED = "required"

    class Selection:
        def __init__(self, **kwargs) -> None:
            self.kwargs = kwargs

    def registration_options(**kwargs):
        captured["registration_options"] = kwargs
        return {
            "challenge": b64url_encode(kwargs["challenge"]),
            "rp": {"id": kwargs["rp_id"]},
            "excludeCredentials": [
                {"id": b64url_encode(item.id), "type": "public-key"}
                for item in (kwargs["exclude_credentials"] or [])
            ],
        }

    def authentication_options(**kwargs):
        captured["authentication_options"] = kwargs
        return {
            "challenge": b64url_encode(kwargs["challenge"]),
            "rpId": kwargs["rp_id"],
            "allowCredentials": [
                {"id": b64url_encode(item.id), "type": "public-key"}
                for item in kwargs["allow_credentials"]
            ],
            "userVerification": kwargs["user_verification"],
        }

    def verify_registration_response(**kwargs):
        captured["verify_registration"] = kwargs
        return SimpleNamespace(
            credential_id=b"credential-id",
            credential_public_key=b"cose-public-key",
            sign_count=1,
            aaguid="aaguid",
            fmt="packed",
            credential_device_type="single_device",
            credential_backed_up=False,
        )

    def verify_authentication_response(**kwargs):
        captured["verify_authentication"] = kwargs
        if "authentication_error" in captured:
            raise captured["authentication_error"]
        return captured.get(
            "authentication_result",
            SimpleNamespace(user_verified=True, new_sign_count=2),
        )

    monkeypatch.setattr(
        "a4p.user_signature.webauthn._load_webauthn",
        lambda: {
            "AuthenticatorSelectionCriteria": Selection,
            "PublicKeyCredentialDescriptor": Descriptor,
            "ResidentKeyRequirement": Requirement,
            "UserVerificationRequirement": Requirement,
            "generate_registration_options": registration_options,
            "generate_authentication_options": authentication_options,
            "verify_registration_response": verify_registration_response,
            "verify_authentication_response": verify_authentication_response,
            "options_to_json": json.dumps,
        },
    )
    return captured


def test_webauthn_registration_options_verify_and_replay(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """WebAuthn 注册使用有效 challenge 完成后应保存凭据，而重复使用同一注册请求应被拒绝。"""
    captured = _mock_webauthn(monkeypatch)
    store = InMemoryCredentialStore()
    method = WebAuthnSignatureMethod(
        store,
        rp_id="example.test",
        expected_origin="https://example.test",
    )
    options = method.registration_options(user_id="user-1")
    challenge = captured["registration_options"]["challenge"]
    credential = {
        "id": b64url_encode(b"credential-id"),
        "response": {"transports": ["internal"]},
    }
    record = method.verify_registration(
        {
            "userId": "user-1",
            "registrationRequestId": options["registrationRequestId"],
            "credential": credential,
        }
    )

    assert captured["verify_registration"]["expected_challenge"] == challenge
    assert captured["verify_registration"]["expected_rp_id"] == "example.test"
    assert captured["verify_registration"]["expected_origin"] == "https://example.test"
    assert record.signatureMethod == "webauthn"
    assert record.publicKey == {
        "format": "cose",
        "value": b64url_encode(b"cose-public-key"),
    }
    assert record.details["signCount"] == 1
    assert record.details["transports"] == ["internal"]

    with pytest.raises(ValueError, match="No WebAuthn registration challenge"):
        method.verify_registration(
            {
                "userId": "user-1",
                "registrationRequestId": options["registrationRequestId"],
                "credential": credential,
            }
        )


def test_webauthn_registration_rejects_invalid_state_and_payload(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """WebAuthn 注册缺少必要字段、用户不匹配或缺少凭据时，应分别拒绝请求。"""
    _mock_webauthn(monkeypatch)
    method = WebAuthnSignatureMethod(InMemoryCredentialStore())

    with pytest.raises(ValueError, match="userId missing"):
        method.verify_registration({})
    with pytest.raises(ValueError, match="registrationRequestId missing"):
        method.verify_registration({"userId": "user-1"})

    mismatch = method.registration_options(user_id="user-1")
    with pytest.raises(ValueError, match="registration user mismatch"):
        method.verify_registration(
            {
                "userId": "user-2",
                "registrationRequestId": mismatch["registrationRequestId"],
                "credential": {},
            }
        )

    missing_credential = method.registration_options(user_id="user-1")
    with pytest.raises(ValueError, match="credential missing"):
        method.verify_registration(
            {
                "userId": "user-1",
                "registrationRequestId": missing_credential[
                    "registrationRequestId"
                ],
            }
        )


def test_webauthn_prepare_requires_registered_method_credential(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """用户仅注册其他签名方法的凭据时，生成 WebAuthn 签名选项应失败。"""
    _mock_webauthn(monkeypatch)
    method = WebAuthnSignatureMethod(
        InMemoryCredentialStore(
            [
                UserCredentialRecord(
                    userId="user-1",
                    credentialId="ed-credential",
                    signatureMethod="ed25519",
                    publicKey={},
                )
            ]
        )
    )
    mandate = create_operation_mandate(
        operation={"action": "read_note", "params": {}},
        server_url="local://test",
        user_signature_method="webauthn",
    )

    with pytest.raises(
        ValueError,
        match="No 'webauthn' credential is registered",
    ):
        method.signing_options(user_id="user-1", mandate=mandate)


def test_webauthn_signing_options_assertion_and_sign_count_update(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """WebAuthn 使用已注册凭据完成断言验证时，应生成正确签名选项并更新签名计数器。"""
    captured = _mock_webauthn(monkeypatch)
    credential_id = b64url_encode(b"credential-id")
    store = InMemoryCredentialStore(
        [
            UserCredentialRecord(
                userId="user-1",
                credentialId=credential_id,
                signatureMethod="webauthn",
                publicKey={
                    "format": "cose",
                    "value": b64url_encode(b"cose-public-key"),
                },
                details={
                    "signCount": 1,
                    "rpId": "example.test",
                    "origin": "https://example.test",
                },
            )
        ]
    )
    method = WebAuthnSignatureMethod(
        store,
        rp_id="example.test",
        expected_origin="https://example.test",
    )
    server = A4PServer(user_signature_method=method)
    prepared = asyncio.run(server.prepare_operation_authorization(_operation_request()))
    assert prepared.mandate is not None
    method_options = prepared.signingOptions["methodOptions"]

    assert prepared.signingOptions["signatureMethod"] == "webauthn"
    assert method_options["allowCredentials"] == [
        {"id": credential_id, "type": "public-key"}
    ]
    assert method_options["userVerification"] == "required"
    assert captured["authentication_options"][
        "challenge"
    ] == derive_user_authorization_challenge(prepared.mandate)

    assertion = {
        "id": credential_id,
        "type": "public-key",
        "response": {},
    }
    signed = sign_user_mandate_with_signer(
        prepared.mandate,
        user_signer=WebAuthnUserSigner(),
        signing_input={"assertion": assertion},
    )
    context = operation_user_signature_context(signed, expected_user_id="user-1")
    valid, reason = method.verify(context, signed["signatures"]["user"])

    assert valid is True, reason
    assert captured["verify_authentication"]["expected_rp_id"] == "example.test"
    assert captured["verify_authentication"]["expected_origin"] == "https://example.test"
    assert captured["verify_authentication"]["require_user_verification"] is True
    updated = store.get(credential_id)
    assert updated is not None
    assert updated.details["signCount"] == 2


def test_webauthn_verify_rejects_credential_binding_and_verifier_failures(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """WebAuthn 验证遇到凭据缺失、方法或用户绑定错误、断言失败或未验证用户时，应全部拒绝。"""
    captured = _mock_webauthn(monkeypatch)
    credential_id = b64url_encode(b"credential-id")
    mandate = create_operation_mandate(
        operation={"action": "read_note", "params": {}},
        server_url="local://test",
        user_signature_method="webauthn",
    )
    context = operation_user_signature_context(mandate, expected_user_id="user-1")
    base_signature = {
        "signatureMethod": "webauthn",
        "credentialId": credential_id,
        "proof": {"assertion": {"id": credential_id}},
    }

    empty_method = WebAuthnSignatureMethod(InMemoryCredentialStore())
    assert empty_method.verify(context, {}) == (
        False,
        "WebAuthn credentialId missing",
    )
    assert empty_method.verify(context, base_signature) == (
        False,
        f"WebAuthn credential not registered: {credential_id}",
    )

    mismatched_method = UserCredentialRecord(
        userId="user-1",
        credentialId=credential_id,
        signatureMethod="ed25519",
        publicKey={},
    )
    method = WebAuthnSignatureMethod(
        InMemoryCredentialStore([mismatched_method])
    )
    assert method.verify(context, base_signature) == (
        False,
        "WebAuthn credential signature method mismatch",
    )

    valid_record = replace(
        mismatched_method,
        userId="user-2",
        signatureMethod="webauthn",
        publicKey={"format": "cose", "value": b64url_encode(b"key")},
    )
    method = WebAuthnSignatureMethod(InMemoryCredentialStore([valid_record]))
    valid, reason = method.verify(context, base_signature)
    assert valid is False
    assert "credential user mismatch" in reason

    method.credential_store.save(replace(valid_record, userId="user-1"))
    captured["authentication_error"] = ValueError("bad assertion")
    valid, reason = method.verify(context, base_signature)
    assert valid is False
    assert reason == "WebAuthn signature invalid: bad assertion"

    captured.pop("authentication_error")
    captured["authentication_result"] = SimpleNamespace(
        user_verified=False,
        new_sign_count=2,
    )
    assert method.verify(context, base_signature) == (
        False,
        "WebAuthn user verification missing",
    )


def test_webauthn_user_signer_requires_assertion_and_credential_id() -> None:
    """WebAuthn 用户签名器缺少 assertion 或 credentialId 时，应拒绝生成签名。"""
    mandate = create_operation_mandate(
        operation={"action": "read_note", "params": {}},
        server_url="local://test",
        user_signature_method="webauthn",
    )
    context = operation_user_signature_context(mandate)
    signer = WebAuthnUserSigner()

    with pytest.raises(ValueError, match="assertion missing"):
        signer.sign(context)
    with pytest.raises(ValueError, match="credentialId missing"):
        signer.sign(context, signing_input={"assertion": {}})


def test_webauthn_rejects_rp_origin_uv_and_bad_assertion(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """WebAuthn 凭据的 RP ID、来源、用户验证要求或断言内容不符合策略时，应拒绝验证。"""
    _mock_webauthn(monkeypatch)
    credential_id = b64url_encode(b"credential-id")
    record = UserCredentialRecord(
        userId="user-1",
        credentialId=credential_id,
        signatureMethod="webauthn",
        publicKey={"format": "cose", "value": b64url_encode(b"key")},
        details={
            "signCount": 0,
            "rpId": "wrong.example",
            "origin": "https://wrong.example",
        },
    )
    method = WebAuthnSignatureMethod(
        InMemoryCredentialStore([record]),
        rp_id="example.test",
        expected_origin="https://example.test",
    )
    mandate = asyncio.run(
        A4PServer(user_signature_method=method).prepare_operation_authorization(
            _operation_request()
        )
    ).mandate
    assert mandate is not None
    context = operation_user_signature_context(mandate, expected_user_id="user-1")
    signature = {
        "signatureMethod": "webauthn",
        "credentialId": credential_id,
        "proof": {"assertion": {"id": credential_id}},
    }

    valid, reason = method.verify(context, signature)
    assert valid is False
    assert "RP ID mismatch" in reason

    method.rp_id = "wrong.example"
    valid, reason = method.verify(context, signature)
    assert valid is False
    assert "origin mismatch" in reason

    method.expected_origin = "https://wrong.example"
    signature["proof"] = {}
    valid, reason = method.verify(context, signature)
    assert valid is False
    assert reason == "WebAuthn assertion missing"


def test_no_signature_mode_rejects_nonempty_user_signature() -> None:
    """operation 处于免用户签名模式但提交了非空用户签名时，应拒绝完成授权。"""
    async def run() -> None:
        server = A4PServer(require_user_signature=False)
        request = _operation_request()
        prepared = await server.prepare_operation_authorization(request)
        assert prepared.mandate is not None
        unsigned = approve_user_mandate(prepared.mandate)
        unsigned["signatures"]["user"] = {"signatureMethod": "ed25519"}
        result = await server.complete_operation_authorization(
            {
                "signedMandate": unsigned,
                "operation": request["operation"],
            }
        )
        assert result.approved is False
        assert result.rejectReason == "User signature must be empty"

    asyncio.run(run())
