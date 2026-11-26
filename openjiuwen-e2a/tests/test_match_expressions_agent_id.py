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
    """service_id 仍被显式拒绝（黑名单保留）。"""
    with pytest.raises(ValueError):
        validate_match_expr("service_id == 'svc1'")
    with pytest.raises(ValueError):
        validate_match_expr("bot_id in ('B') and service_id == 'svc1'")


def test_missing_agent_id_does_not_match():
    """identity 缺 agent_id 字段时，agent_id 表达式不命中（_MISSING 语义）。"""
    ident = {"user_id": "u2", "group_id": "g1", "bot_id": "A"}
    assert matches("agent_id == 'agent1'", ident) is False
    assert matches("agent_id != 'agent1'", ident) is False
    assert matches("agent_id in ('agent1', 'agent2')", ident) is False
