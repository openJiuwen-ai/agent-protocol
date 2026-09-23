"""Tests for sanitize_tool_calls purity（意见9：不得原地污染调用方消息）."""
import copy

from intelli_router.core.deployment import Deployment
from intelli_router.provider.base_provider import BaseProviderAdapter
from intelli_router.provider.siliconflow_provider import SiliconFlowProviderAdapter


def _make_messages():
    """构造含非标准字段的 assistant tool_calls 消息列表（每次返回全新对象）。"""
    return [
        {"role": "user", "content": "check the weather"},
        {
            "role": "assistant",
            "content": None,
            "tool_calls": [
                {
                    "id": "call_1",
                    "type": "function",
                    "index": 0,
                    "function": {"name": "get_weather", "arguments": "{\"city\": \"SF\"}"},
                },
                {
                    # 无 id 的调用应补 ""
                    "type": "function",
                    "index": 1,
                    "function": {"name": "get_time", "arguments": "{}"},
                },
            ],
        },
        {"role": "tool", "tool_call_id": "call_1", "content": "sunny"},
    ]


class TestSanitizeToolCallsPurity:
    def test_does_not_mutate_caller_messages(self):
        """(a) sanitize_tool_calls 不修改调用方原始列表，index 等字段保留在原对象中。"""
        messages = _make_messages()
        snapshot = copy.deepcopy(messages)

        BaseProviderAdapter.sanitize_tool_calls(messages)

        assert messages == snapshot
        # 原始 assistant dict 的 tool_calls 仍含非标准字段 index
        assert messages[1]["tool_calls"][0]["index"] == 0
        assert messages[1]["tool_calls"][1]["index"] == 1

    def test_sanitized_result_is_normalized(self):
        """(c) sanitize 后的消息规范化：type=function、无 id 补 ""、丢弃 index。"""
        messages = _make_messages()

        result = BaseProviderAdapter.sanitize_tool_calls(messages)

        assistant = result[1]
        assert result[0] is messages[0]  # 非 assistant 消息原样保留引用
        assert result[2] is messages[2]
        assert len(assistant["tool_calls"]) == 2
        tc0, tc1 = assistant["tool_calls"]
        assert tc0 == {
            "id": "call_1",
            "type": "function",
            "function": {"name": "get_weather", "arguments": "{\"city\": \"SF\"}"},
        }
        assert "index" not in tc0
        assert tc1["id"] == ""  # 无 id 补 ""
        assert tc1["type"] == "function"
        assert "index" not in tc1

    def test_reuse_messages_across_transform_request_calls(self):
        """(b) 同一 messages 列表对 SiliconFlow 适配器调两次，原始对象不污染且两次结果一致。"""
        adapter = SiliconFlowProviderAdapter()
        dep = Deployment(
            model_name="Qwen/Qwen2.5-72B-Instruct",
            api_key="sk-test",
            api_base="https://api.siliconflow.cn",
        )
        messages = _make_messages()
        snapshot = copy.deepcopy(messages)

        body1 = adapter.transform_request(
            model="Qwen/Qwen2.5-72B-Instruct",
            messages=messages,
            deployment=dep,
        )
        assert messages == snapshot  # 第一次调用后未被污染

        body2 = adapter.transform_request(
            model="Qwen/Qwen2.5-72B-Instruct",
            messages=messages,
            deployment=dep,
        )

        assert messages == snapshot  # 两次调用后仍未被污染
        assert body1["messages"] == body2["messages"]
        # 请求体中的 tool_calls 已清洗
        assert "index" not in body1["messages"][1]["tool_calls"][0]
        assert body1["messages"][1]["tool_calls"][0]["type"] == "function"
