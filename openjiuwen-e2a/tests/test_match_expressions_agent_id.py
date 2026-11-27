# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
"""issue #124: agent_id 字段支持验证。

历史：_ALLOWED_MATCH_NAMES 仅含 user_id/group_id/bot_id，显式拒绝 agent_id；
运行时 identity 也不带 agent_id，导致按 Agent 匹配的策略无法命中
（issue #124 标题现象）。

修复：_ALLOWED_MATCH_NAMES 加 agent_id，移除对 agent_id 的黑名单（保留
service_id 黑名单）。
"""
import pytest

from e2a.match_expressions import matches, validate_match_expr

IDENTITY = {
    "user_id": "u2",
    "group_id": "g1",
    "bot_id": "A",
    "agent_id": "agent1",
}


@pytest.mark.parametrize("expr, expected", [
    (None, True), ("", True), ([], True), ("[]", True),
    ("user_id == 'u1'", False), ("user_id != 'u1'", True),
    ("(user_id == 'u1' or user_id == 'u2') and group_id == 'g1'", True),
    ("user_id == 'u2' and group_id == 'g2'", False),
    (["user_id == 'u1'", "group_id == 'g1'"], True),
    ('["user_id == \'u1\'", "bot_id == \'B\'"]', False),
    ("user_id in ('u1', 'u2')", True),
    ("user_id in ['u1', 'u3']", False),
    ("group_id not in ['g2', 'g3']", True),
    ("user_id in ('u2')", True),
    ("user_id in ('u22')", False),
    ("user_id in 'u2xx'", False),
    ("user_id not in 'u2xx'", True),
    ("False", False), ("True", True),
    ("user_id == group_id == 'g1'", False),
    # agent_id 匹配（issue #124 核心诉求）
    ("agent_id == 'agent1'", True),
    ("agent_id != 'agent1'", False),
    ("agent_id != 'agent0'", True),
    ("agent_id in ('agent1', 'agent2')", True),
    ("agent_id in ('agent1')", True),
    ("agent_id in ('agent2')", False),
    ("agent_id not in ('agent2')", True),
    # agent_id 与其他字段组合
    ("user_id == 'u2' and agent_id == 'agent1'", True),
    ("user_id == 'u2' and agent_id == 'agent2'", False),
    ("agent_id == 'agent1' or user_id == 'u1'", True),
])
def test_matches(expr, expected):
    assert matches(expr, IDENTITY) is expected


def test_agent_id_allowed_in_match_expr():
    """agent_id 现已纳入 _ALLOWED_MATCH_NAMES（issue #124），validate 不再拒绝。"""
    validate_match_expr("agent_id == 'agent1'")
    validate_match_expr("agent_id in ('agent1', 'agent2')")
    validate_match_expr("user_id == 'u2' and agent_id == 'agent1'")


def test_service_id_still_rejected():
    """service_id 仍被显式拒绝（黑名单保留）。

    覆盖两条拒绝路径，避免单一路径使该条目被误删后测试仍通过：
    1. 表达式含 ==/!=/in：走 AST allowlist，报 "unknown name 'service_id'"
       （_ALLOWED_MATCH_NAMES 不含 service_id）。
    2. 表达式仅含裸字段名（无 ==/!=/in）：走 _ALWAYS_MATCH_FORBIDDEN_CHECKS
       黑名单，报 "service_id is not allowed"。此处真正覆盖黑名单分支，
       删掉该条目后错误文案会变为 "only comparison expressions..."，
       match= 断言会失败。
    """
    # 路径 1：AST allowlist 拒绝
    with pytest.raises(ValueError, match=r"unknown name 'service_id'"):
        validate_match_expr("service_id == 'svc1'")
    # 路径 2：黑名单拒绝（覆盖 _ALWAYS_MATCH_FORBIDDEN_CHECKS 中的 service_id 条目）
    with pytest.raises(ValueError, match=r"service_id is not allowed"):
        validate_match_expr("service_id")


def test_missing_agent_id_does_not_match():
    """identity 缺 agent_id 字段时，agent_id 表达式不命中（_MISSING 语义）。"""
    ident = {"user_id": "u2", "group_id": "g1", "bot_id": "A"}
    assert matches("agent_id == 'agent1'", ident) is False
    assert matches("agent_id != 'agent1'", ident) is False
    assert matches("agent_id in ('agent1', 'agent2')", ident) is False


def test_agent_id_end_to_end_via_web_identity():
    """agent_id 经 web 身份组装链路后仍可命中（issue #124 端到端）。

    回应 P2 检视意见：normalize_routing_identity → apply_routing_metadata →
    web_routing_identity 全链路不应丢失 agent_id，否则 matches 因字段缺失
    而 fail-closed，按 Agent 匹配的策略仍命中不了。
    """
    from e2a.identity import (
        apply_routing_metadata,
        normalize_routing_identity,
        web_routing_identity,
    )

    raw = {"user_id": "u2", "group_id": "g1", "bot_id": "A", "agent_id": "agent1"}

    identity = normalize_routing_identity(raw)
    assert identity.get("agent_id") == "agent1"

    meta = apply_routing_metadata(None, identity)
    round_trip = web_routing_identity(meta)
    assert round_trip.get("agent_id") == "agent1"

    assert matches("agent_id == 'agent1'", round_trip) is True
    assert matches("agent_id == 'agent2'", round_trip) is False
    assert matches("user_id == 'u2' and agent_id == 'agent1'", round_trip) is True
