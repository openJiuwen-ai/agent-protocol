from __future__ import annotations

import asyncio

import pytest

from a4p import approve_user_mandate
from a4p.intent import mandate as intent_mandate
from a4p.intent import token as intent_token
from a4p.intent.usage_store import (
    IntentTokenUsageStoreError,
    SQLiteIntentTokenUsageStore,
)
from tests.support import ExplicitEd25519A4PServer as A4PServer
from tests.support import sign_user_mandate


async def _prepare_and_complete_intent(server: A4PServer, request: dict):  # noqa: ANN001
    prepared = await server.prepare_intent_authorization(request)
    assert prepared.mandate is not None
    signed = sign_user_mandate(prepared.mandate)
    return await server.complete_intent_authorization({"signedMandate": signed})


def test_prepare_complete_intent_preserves_agent_key_binding() -> None:
    """准备并完成 intent 授权时，生成的 mandate 和 token 应保留并校验 Agent 公钥绑定。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "agentPublicKey": {
                    "kty": "OKP",
                    "kid": "agent-key-1",
                    "crv": "Ed25519",
                    "x": "abc",
                },
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert prepared.mandate["subject"]["agentKey"]["kid"] == "agent-key-1"
        signed = sign_user_mandate(prepared.mandate)

        completed = await server.complete_intent_authorization(
            {"signedMandate": signed}
        )

        assert completed.approved is True
        assert completed.intentToken is not None
        assert completed.mandate is not None
        assert completed.mandate["signatures"]["server"]["alg"] == "EdDSA"
        assert completed.mandate["signatures"]["user"]["proof"]["alg"] == "EdDSA"
        assert completed.intentToken["alg"] == "EdDSA"
        assert completed.intentToken["subject"]["agentKey"]["kid"] == "agent-key-1"
        valid, reason = intent_token.verify_intent_token(
            completed.intentToken,
            action="delete_note",
            params={"note_id": "note-1"},
            expected_agent_key_id="agent-key-1",
        )
        assert valid is True, reason

    asyncio.run(run())


@pytest.mark.parametrize(
    ("request_payload", "reason"),
    [
        (
            {
                "userId": "user-1",
                "intent": {"actions": [{"name": "delete_note", "params": {}}]},
            },
            "agentId missing",
        ),
        (
            {
                "agentId": "agent-1",
                "intent": {"actions": [{"name": "delete_note", "params": {}}]},
            },
            "userId missing",
        ),
        (
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {"actions": [{"name": "delete_note", "params": {}}]},
                "validitySeconds": 0,
            },
            "validitySeconds must be a positive integer",
        ),
        (
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {"actions": [{"name": "delete_note", "params": {}}]},
                "validitySeconds": True,
            },
            "validitySeconds must be a positive integer",
        ),
        (
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {"actions": [{"name": "delete_note", "params": {}}]},
                "validitySeconds": "60",
            },
            "validitySeconds must be a positive integer",
        ),
    ],
)
def test_prepare_intent_rejects_missing_identity_and_invalid_validity(
    request_payload: dict[str, object],
    reason: str,
) -> None:
    """准备 intent 授权时缺少身份字段或有效期非法，应拒绝请求并返回对应原因。"""
    prepared = asyncio.run(A4PServer().prepare_intent_authorization(request_payload))

    assert prepared.mandate is None
    assert prepared.rejectReason == reason


def test_missing_user_signature_method_is_rejected() -> None:
    """创建要求用户签名但未指定签名方法的 intent mandate 时，应立即拒绝。"""
    mandate = intent_mandate.create_intent_mandate(
        server="local://test",
        agent_id="agent-1",
        actions=[{"name": "delete_note", "params": {}}],
        user_signature_method="ed25519",
    )
    del mandate["userAuthorization"]["signatureMethod"]
    mandate = intent_mandate.sign_server_mandate(mandate)

    valid, reason = intent_mandate.verify_intent_mandate(
        mandate,
        expected_server="local://test",
    )

    assert valid is False
    assert reason == "userAuthorization.signatureMethod missing"


def test_complete_intent_rejects_mandate_from_another_pending_request() -> None:
    """使用另一个待处理请求的 mandate 完成 intent 授权时，应因请求不匹配而拒绝。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        broad = await server.prepare_intent_authorization(
            {
                "agentId": "agent-a",
                "userId": "user-a",
                "intent": {"actions": [{"name": "delete_all", "params": "*"}]},
                "validitySeconds": 60,
            }
        )
        benign = await server.prepare_intent_authorization(
            {
                "agentId": "agent-b",
                "userId": "user-b",
                "intent": {
                    "actions": [{"name": "read_note", "params": {"note_id": "note-1"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert broad.mandate is not None
        assert benign.mandate is not None
        broad_signed = sign_user_mandate(broad.mandate)
        broad_signed["mandateId"] = benign.mandate["mandateId"]

        mismatched = await server.complete_intent_authorization(
            {"signedMandate": broad_signed}
        )
        benign_signed = sign_user_mandate(benign.mandate)
        completed = await server.complete_intent_authorization(
            {"signedMandate": benign_signed}
        )

        assert mismatched.approved is False
        assert mismatched.verificationResult is not None
        assert mismatched.verificationResult.code == "MANDATE_PENDING_MISMATCH"
        assert completed.approved is True
        assert completed.intentToken is not None
        assert completed.intentToken["user"]["id"] == "user-b"
        assert completed.intentToken["subject"]["id"] == "agent:agent-b"

    asyncio.run(run())


def test_complete_intent_rejects_unknown_mandate_id() -> None:
    """使用未知 mandateId 完成 intent 授权时，应按失败关闭原则拒绝。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "read_note", "params": {"note_id": "note-1"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        signed = sign_user_mandate(prepared.mandate)
        signed["mandateId"] = "mdt_unknown"

        completed = await server.complete_intent_authorization(
            {"signedMandate": signed}
        )

        assert completed.approved is False
        assert completed.verificationResult is not None
        assert completed.verificationResult.code == "AUTHORIZATION_NOT_PENDING"

    asyncio.run(run())


def test_prepare_generates_independent_pending_mandate_ids() -> None:
    """连续准备多个 intent 授权时，应为每个待处理请求生成互不相同的 mandateId。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        first = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "read_note", "params": {"note_id": "note-1"}}]
                },
                "validitySeconds": 60,
            }
        )
        duplicate = await server.prepare_intent_authorization(
            {
                "agentId": "agent-2",
                "userId": "user-2",
                "intent": {"actions": [{"name": "delete_all", "params": "*"}]},
                "validitySeconds": 60,
            }
        )
        assert first.mandate is not None
        assert duplicate.mandate is not None
        assert duplicate.mandate["mandateId"] != first.mandate["mandateId"]
        signed = sign_user_mandate(first.mandate)
        completed = await server.complete_intent_authorization(
            {"signedMandate": signed}
        )

        assert completed.approved is True

    asyncio.run(run())


def test_intent_execution_policy_is_signed_and_copied_to_token() -> None:
    """intent 包含执行策略时，该策略应受 mandate 签名保护并原样写入签发的 token。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}],
                    "executionPolicy": {"maxExecutions": 2},
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        policy = prepared.mandate["intent"]["executionPolicy"]
        assert policy == {"maxExecutions": 2}
        assert "最多 2 次" in prepared.mandate["displayText"]

        signed = sign_user_mandate(prepared.mandate)
        completed = await server.complete_intent_authorization(
            {"signedMandate": signed}
        )

        assert completed.approved is True
        assert completed.intentToken is not None
        assert completed.intentToken["intent"]["executionPolicy"] == policy

        tampered_token = dict(completed.intentToken)
        tampered_token["intent"] = dict(completed.intentToken["intent"])
        tampered_token["intent"]["executionPolicy"] = dict(policy)
        tampered_token["intent"]["executionPolicy"]["maxExecutions"] = 3
        valid, reason = intent_token.verify_intent_token(
            tampered_token,
            action="delete_note",
            params={"note_id": "note-1"},
        )
        assert valid is False
        assert "signature invalid" in reason

    asyncio.run(run())


def test_intent_execution_policy_ignores_unknown_fields() -> None:
    """规范化 intent 执行策略时，应保留受支持字段并忽略未知字段。"""
    expected = {"maxExecutions": 2}

    assert (
        intent_mandate.normalize_execution_policy(
            {
                "maxExecutions": 2,
                "period": {"periodSeconds": 86400, "maxExecutions": 1},
            }
        )
        == expected
    )
    assert (
        intent_mandate.normalize_execution_policy(
            {"maxExecutions": 2, "unknownPolicyOption": True}
        )
        == expected
    )


def test_intent_execution_policy_total_cap_is_consumed_by_server_verification() -> None:
    """服务端验证带总执行次数上限的 intent token 时，应逐次消费配额并在超限后拒绝。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        auth = await _prepare_and_complete_intent(
            server,
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}],
                    "executionPolicy": {"maxExecutions": 2},
                },
                "validitySeconds": 60,
            },
        )
        assert auth.intentToken is not None

        first = await server.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "delete_note", "params": {"note_id": "note-1"}},
            }
        )
        second = await server.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "delete_note", "params": {"note_id": "note-2"}},
            }
        )
        third = await server.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "delete_note", "params": {"note_id": "note-3"}},
            }
        )

        assert first.valid is True
        assert first.matchedScope is not None
        assert first.matchedScope["usage"] == {
            "executionsUsed": 1,
            "executionsLimit": 2,
        }
        assert second.valid is True
        assert third.valid is False
        assert third.code == "TOKEN_USAGE_EXCEEDED"

    asyncio.run(run())


def test_intent_execution_policy_usage_persists_across_server_instances(
    tmp_path,
) -> None:
    """多个服务实例共享 SQLite 用量库时，intent token 的已消费配额应跨实例持久化。"""
    async def run() -> None:
        path = tmp_path / "usage.sqlite3"
        first_server = A4PServer(
            server_id="local://test",
            intent_token_usage_store=SQLiteIntentTokenUsageStore(path),
        )
        auth = await _prepare_and_complete_intent(
            first_server,
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}],
                    "executionPolicy": {"maxExecutions": 1},
                },
                "validitySeconds": 60,
            },
        )
        assert auth.intentToken is not None
        request = {
            "token": auth.intentToken,
            "expected": {"action": "delete_note", "params": {"note_id": "note-1"}},
        }

        first = await first_server.verify_intent_token(request)
        restarted_server = A4PServer(
            server_id="local://test",
            intent_token_usage_store=SQLiteIntentTokenUsageStore(path),
        )
        after_restart = await restarted_server.verify_intent_token(request)

        assert first.valid is True
        assert after_restart.valid is False
        assert after_restart.code == "TOKEN_USAGE_EXCEEDED"

    asyncio.run(run())


def test_intent_usage_store_failure_is_fail_closed() -> None:
    """intent 用量存储发生故障时，token 验证应失败关闭而不能绕过执行策略。"""
    class FailingUsageStore:
        def consume(self, **_kwargs):  # noqa: ANN003, ANN201
            raise IntentTokenUsageStoreError("database unavailable")

    async def run() -> None:
        server = A4PServer(
            server_id="local://test",
            intent_token_usage_store=FailingUsageStore(),
        )
        auth = await _prepare_and_complete_intent(
            server,
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}],
                    "executionPolicy": {"maxExecutions": 1},
                },
                "validitySeconds": 60,
            },
        )
        assert auth.intentToken is not None

        verified = await server.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "delete_note", "params": {"note_id": "note-1"}},
            }
        )

        assert verified.valid is False
        assert verified.code == "TOKEN_USAGE_STORE_ERROR"
        assert verified.reason == "Intent token usage store unavailable"

    asyncio.run(run())


def test_intent_without_execution_policy_does_not_access_usage_store() -> None:
    """intent 未配置执行策略时，token 验证应成功且不访问用量存储。"""
    class UnexpectedUsageStore:
        def consume(self, **_kwargs):  # noqa: ANN003, ANN201
            raise AssertionError("usage store must not be called")

    async def run() -> None:
        server = A4PServer(
            server_id="local://test",
            intent_token_usage_store=UnexpectedUsageStore(),
        )
        auth = await _prepare_and_complete_intent(
            server,
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            },
        )
        assert auth.intentToken is not None

        verified = await server.verify_intent_token(
            {
                "token": auth.intentToken,
                "expected": {"action": "delete_note", "params": {"note_id": "note-1"}},
            }
        )

        assert verified.valid is True
        assert verified.matchedScope is not None
        assert "usage" not in verified.matchedScope

    asyncio.run(run())


def test_invalid_intent_execution_policies_reject_mandate_creation() -> None:
    """intent 执行策略包含非法上限或时间窗口时，mandate 创建应拒绝这些配置。"""
    async def run() -> None:
        cases = [
            {"maxExecutions": 0},
            {"maxExecutions": -1},
            {"maxExecutions": 1.5},
            {"maxExecutions": "2"},
            None,
            {},
            {"period": {"periodSeconds": 60, "maxExecutions": 1}},
            {"unknownPolicyOption": True},
            [],
        ]
        for policy in cases:
            server = A4PServer(server_id="local://test")
            prepared = await server.prepare_intent_authorization(
                {
                    "agentId": "agent-1",
                    "userId": "user-1",
                    "intent": {
                        "actions": [
                            {"name": "delete_note", "params": {"note_id": "*"}}
                        ],
                        "executionPolicy": policy,
                    },
                    "validitySeconds": 60,
                }
            )

            assert prepared.approved is False
            assert prepared.mandate is None
            assert prepared.verificationResult is not None
            assert prepared.verificationResult.code == "MANDATE_INVALID"

    asyncio.run(run())


def test_intent_no_signature_prepare_and_complete_issues_token() -> None:
    """服务端启用免用户签名模式时，准备并完成 intent 授权应成功签发 token。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test", require_user_signature=False)
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert prepared.mandate["userAuthorization"] == {"required": False}
        approved = approve_user_mandate(prepared.mandate)

        completed = await server.complete_intent_authorization(
            {"signedMandate": approved}
        )

        assert completed.approved is True
        assert completed.intentToken is not None
        assert completed.mandate is not None
        assert completed.mandate["signatures"]["user"] == {}

    asyncio.run(run())


def test_intent_no_signature_approval_rejected_when_signature_required() -> None:
    """服务端要求用户签名时，使用免签名方式批准 intent 应被拒绝。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test")
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert prepared.mandate["userAuthorization"]["required"] is True
        approved = approve_user_mandate(prepared.mandate)

        completed = await server.complete_intent_authorization(
            {"signedMandate": approved}
        )

        assert completed.approved is False
        assert completed.verificationResult is not None
        assert completed.verificationResult.code == "MANDATE_SIGNATURE_INVALID"
        assert completed.rejectReason == "User signature missing"

    asyncio.run(run())


def test_intent_no_signature_primitive_token_issue_requires_explicit_opt_out() -> None:
    """底层接口为无用户签名的 intent mandate 签发 token 时，必须显式声明免签名模式。"""
    mandate = intent_mandate.create_intent_mandate(
        server="local://test",
        agent_id="agent-1",
        actions=[{"name": "delete_note", "params": {"note_id": "*"}}],
        validity_seconds=60,
        require_user_signature=False,
    )
    approved = approve_user_mandate(mandate)

    with pytest.raises(ValueError, match="User signature method missing"):
        intent_token.issue_intent_token(approved, user_id="user-1")

    token = intent_token.issue_intent_token(
        approved,
        user_id="user-1",
        require_user_signature=False,
    )

    assert token["type"] == "a4p/v1/intent-token"


def test_intent_no_signature_mode_rejects_nonempty_user_signature() -> None:
    """服务端启用免用户签名模式时，提交非空用户签名应被拒绝。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test", require_user_signature=False)
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        approved = approve_user_mandate(prepared.mandate)
        approved["signatures"]["user"] = {"signatureMethod": "ed25519"}

        completed = await server.complete_intent_authorization(
            {"signedMandate": approved}
        )

        assert completed.approved is False
        assert completed.rejectReason == "User signature must be empty"

    asyncio.run(run())


def test_intent_no_signature_still_rejects_tampered_mandate_core() -> None:
    """即使启用免用户签名模式，篡改 mandate 核心字段的 intent 授权仍应被拒绝。"""
    async def run() -> None:
        server = A4PServer(server_id="local://test", require_user_signature=False)
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "intent": {
                    "actions": [{"name": "delete_note", "params": {"note_id": "*"}}]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        approved = approve_user_mandate(prepared.mandate)
        approved["displayText"] = "tampered"

        completed = await server.complete_intent_authorization(
            {"signedMandate": approved}
        )

        assert completed.approved is False
        assert completed.verificationResult is not None
        assert completed.verificationResult.code == "MANDATE_PENDING_MISMATCH"
        assert (
            completed.rejectReason
            == "Signed mandate does not match pending intent authorization"
        )

    asyncio.run(run())


def test_custom_intent_display_text_renderer_is_signed() -> None:
    """使用自定义 intent 展示文本渲染器时，渲染结果应写入 mandate 并受到服务端签名保护。"""
    async def run() -> None:
        seen_mandates = []

        def payment_display_text(mandate: dict[str, object]) -> str:
            seen_mandates.append(mandate)
            intent = mandate["intent"]
            assert isinstance(intent, dict)
            actions = intent["actions"]
            assert isinstance(actions, list)
            params = actions[0]["params"]
            assert isinstance(params, dict)
            text = (
                f"授权 {params['merchant']} 支付，最高金额 "
                f"{int(params['max_amount_cents']) / 100:.2f} {params['currency']}"
            )
            params["max_amount_cents"] = 1
            return text

        server = A4PServer(
            server_id="local://test",
            intent_display_text_renderer=payment_display_text,
        )
        prepared = await server.prepare_intent_authorization(
            {
                "agentId": "payment-agent",
                "userId": "user-1",
                "intent": {
                    "actions": [
                        {
                            "name": "pay_order",
                            "params": {
                                "merchant": "luckin",
                                "currency": "CNY",
                                "max_amount_cents": 2000,
                            },
                        }
                    ]
                },
                "validitySeconds": 60,
            }
        )
        assert prepared.mandate is not None
        assert seen_mandates
        assert prepared.mandate["displayText"] == "授权 luckin 支付，最高金额 20.00 CNY"
        assert (
            prepared.mandate["intent"]["actions"][0]["params"]["max_amount_cents"]
            == 2000
        )

        signed = sign_user_mandate(prepared.mandate)
        completed = await server.complete_intent_authorization(
            {"signedMandate": signed}
        )

        assert completed.approved is True
        assert completed.intentToken is not None

    asyncio.run(run())
