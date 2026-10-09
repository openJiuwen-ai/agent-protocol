from __future__ import annotations

import asyncio
import copy


from a4p.intent import token as intent_token
from tests.support import ExplicitEd25519A4PServer as A4PServer
from tests.support import sign_user_mandate


def _token_with_action_params(constraint):
    """Build a minimal intent token dict for params_match_intent_token tests."""
    return {"intent": {"actions": [{"name": "act", "params": constraint}]}}


def test_params_match_intent_token_glob_and_exact() -> None:
    """intent 参数约束包含整体通配符、字段通配符或精确值时，应按各自规则接受匹配参数并拒绝不匹配参数。"""
    cases = [
        # (constraint, actual_params, expected_ok, description)
        ("*", {"x": 1}, True, "whole-params wildcard allows anything"),
        ({"k": "*"}, {"k": "anything"}, True, "per-key wildcard any value"),
        ({"k": "*"}, {"k": 123}, True, "per-key wildcard non-string value"),
        ({"k": "note-1"}, {"k": "note-1"}, True, "exact string match"),
        ({"k": "note-1"}, {"k": "note-2"}, False, "exact string mismatch"),
        ({"k": "*.md"}, {"k": "example.md"}, True, "glob suffix match"),
        ({"k": "*.md"}, {"k": "example.txt"}, False, "glob suffix mismatch"),
        ({"k": "note-*"}, {"k": "note-1"}, True, "glob prefix match"),
        ({"k": "v?.0"}, {"k": "v1.0"}, True, "glob single-char match"),
        ({"k": "v?.0"}, {"k": "v12.0"}, False, "glob single-char mismatch"),
        ({"k": "*"}, {}, False, "missing required param rejected"),
        (
            {"k": "*"},
            {"k": "x", "extra": 1},
            False,
            "extra unauthorized param rejected",
        ),
        ({"k": 42}, {"k": 42}, True, "non-string exact equality"),
        ({"k": 42}, {"k": 43}, False, "non-string mismatch"),
        ({"k": "Example.MD"}, {"k": "example.md"}, False, "case-sensitive glob"),
    ]
    for constraint, actual, expected_ok, desc in cases:
        token = _token_with_action_params(constraint)
        ok, reason = intent_token.params_match_intent_token(
            token, action="act", params=actual
        )
        assert ok is expected_ok, (
            f"{desc}: constraint={constraint!r} actual={actual!r} -> ok={ok}, reason={reason!r}"
        )

    # action not in token allowlist
    token = _token_with_action_params({"k": "*"})
    ok, reason = intent_token.params_match_intent_token(
        token, action="other", params={"k": "x"}
    )
    assert ok is False, "action not in allowlist should be rejected"
    assert "not in token actions" in reason


def test_params_match_intent_token_checks_all_same_name_candidates() -> None:
    """intent 中存在多个同名 action 候选时，只要任一候选参数匹配就应验证成功。"""
    cases = [
        (
            [
                {"name": "act", "params": {"kind": "first"}},
                {"name": "act", "params": {"kind": "second"}},
            ],
            {"kind": "second"},
            "value mismatch",
        ),
        (
            [
                {"name": "act", "params": {"kind": "first", "required": "yes"}},
                {"name": "act", "params": {"kind": "first"}},
            ],
            {"kind": "first"},
            "missing required param",
        ),
        (
            [
                {"name": "act", "params": {"kind": "first"}},
                {"name": "act", "params": {"kind": "first"}, "allowExtraParams": True},
            ],
            {"kind": "first", "optional": True},
            "unexpected param",
        ),
        (
            [
                {"name": "act", "params": {"kind": "first"}, "allowExtraParams": True},
                {"name": "act", "params": {"kind": "second"}},
            ],
            {"kind": "second"},
            "allowExtraParams candidate value mismatch",
        ),
    ]
    for actions, actual, description in cases:
        token = {"intent": {"actions": actions}}
        ok, reason = intent_token.params_match_intent_token(
            token, action="act", params=actual
        )
        assert ok is True, f"{description}: reason={reason!r}"
        assert reason == ""


def test_params_match_intent_token_all_same_name_candidates_mismatch() -> None:
    """intent 中多个同名 action 的参数均不匹配时，应返回所有候选均不匹配的失败结果。"""
    token = {
        "intent": {
            "actions": [
                {"name": "act", "params": {"kind": "first"}},
                {"name": "act", "params": {"required": "yes"}},
            ]
        }
    }

    ok, reason = intent_token.params_match_intent_token(
        token, action="act", params={"kind": "other"}
    )

    assert ok is False
    assert reason == "Param 'kind' mismatch for action 'act'"


def _token_with_action(constraint, allow_extra=False):
    """Build a minimal intent token dict with optional allowExtraParams flag."""
    action = {"name": "act", "params": constraint}
    if allow_extra:
        action["allowExtraParams"] = True
    return {"intent": {"actions": [action]}}


def test_params_match_intent_token_allow_extra() -> None:
    """匹配 intent 参数时，应根据 allowExtraParams 配置决定是否接受约束之外的额外参数。"""
    cases = [
        # (constraint, allow_extra, actual_params, expected_ok, description)
        (
            {"k": "v"},
            True,
            {"k": "v", "extra": 1},
            True,
            "allowExtraParams=true allows extra param",
        ),
        (
            {"k": "v"},
            False,
            {"k": "v", "extra": 1},
            False,
            "default rejects extra param",
        ),
        (
            {"k": "v"},
            True,
            {"extra": 1},
            False,
            "required key still enforced with allowExtraParams",
        ),
        (
            {"k": "v"},
            True,
            {"k": "other", "extra": 1},
            False,
            "value mismatch still rejected with allowExtraParams",
        ),
        (
            {"k": "v"},
            True,
            {"k": "v"},
            True,
            "allowExtraParams=true with no extra params",
        ),
        (
            {"k": "*.md"},
            True,
            {"k": "a.md", "extra": 1},
            True,
            "glob constraint + extra allowed",
        ),
        (
            {"k": "*.md"},
            True,
            {"k": "a.txt", "extra": 1},
            False,
            "glob mismatch + extra still rejected",
        ),
        (
            {"command": "ls"},
            True,
            {"command": "ls", "description": "x", "timeout": 10},
            True,
            "bash scenario: only command declared, extras allowed",
        ),
        (
            {"command": "ls"},
            True,
            {"command": "rm", "description": "x"},
            False,
            "bash scenario: command value mismatch rejected",
        ),
    ]
    for constraint, allow_extra, actual, expected_ok, desc in cases:
        token = _token_with_action(constraint, allow_extra=allow_extra)
        ok, reason = intent_token.params_match_intent_token(
            token, action="act", params=actual
        )
        assert ok is expected_ok, (
            f"{desc}: constraint={constraint!r} allow_extra={allow_extra} actual={actual!r} -> ok={ok}, reason={reason!r}"
        )


def test_allow_extra_params_field_is_signature_protected() -> None:
    """签发 token 后篡改 allowExtraParams 字段时，签名校验应失败并拒绝扩展授权范围。"""
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
                    "actions": [
                        {
                            "name": "bash",
                            "params": {"command": "ls"},
                            "allowExtraParams": True,
                        }
                    ]
                },
                "validitySeconds": 60,
            }
        )
        signed = sign_user_mandate(prepared.mandate)
        completed = await server.complete_intent_authorization(
            {"signedMandate": signed}
        )
        assert completed.approved is True
        token = completed.intentToken
        assert token is not None

        # Original token: extra params allowed
        ok, reason = intent_token.verify_intent_token(
            token,
            action="bash",
            params={"command": "ls", "description": "x", "timeout": 10},
            expected_agent_key_id="agent-key-1",
        )
        assert ok is True, f"original token should allow extra params: {reason}"

        # Tamper: flip allowExtraParams to False -> signature must fail
        tampered = copy.deepcopy(token)
        tampered["intent"]["actions"][0]["allowExtraParams"] = False
        ok_tampered, reason_tampered = intent_token.verify_intent_token(
            tampered,
            action="bash",
            params={"command": "ls", "description": "x"},
            expected_agent_key_id="agent-key-1",
        )
        assert ok_tampered is False, "tampered allowExtraParams should break signature"
        assert "signature" in reason_tampered.lower(), (
            f"expected signature failure, got: {reason_tampered}"
        )

    asyncio.run(run())
