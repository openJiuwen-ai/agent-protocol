"""AssistantMessageChunk 流式累积（__add__）测试：tool_calls 与 usage_metadata 合并。"""
from intelli_router.types import AssistantMessageChunk, ToolCall, UsageMetadata


# -------- 意见 10: tool_calls 合并不可改写操作数 --------

def test_add_merges_tool_call_arguments():
    base = AssistantMessageChunk(tool_calls=[ToolCall(id="call_1", name="get_weather", arguments='{"a', index=0)])
    delta = AssistantMessageChunk(tool_calls=[ToolCall(id="", arguments='":1}', index=0)])

    merged = base + delta

    assert merged.tool_calls is not None
    assert len(merged.tool_calls) == 1
    assert merged.tool_calls[0].arguments == '{"a":1}'
    assert merged.tool_calls[0].id == "call_1"
    assert merged.tool_calls[0].name == "get_weather"


def test_add_does_not_mutate_tool_call_operands():
    base = AssistantMessageChunk(tool_calls=[ToolCall(id="call_1", name="get_weather", arguments='{"a', index=0)])
    delta = AssistantMessageChunk(tool_calls=[ToolCall(id="", arguments='":1}', index=0)])

    merged = base + delta

    assert base.tool_calls[0].arguments == '{"a'
    assert delta.tool_calls[0].arguments == '":1}'
    # merged 中的 ToolCall 是新实例，不与操作数共享同一对象
    assert merged.tool_calls[0] is not base.tool_calls[0]
    assert merged.tool_calls[0] is not delta.tool_calls[0]


def test_repeated_add_yields_consistent_result():
    """重复 base+delta 两次：base 不被第一次累加改写，两次结果一致。"""
    base = AssistantMessageChunk(tool_calls=[ToolCall(id="call_1", name="get_weather", arguments='{"a', index=0)])
    delta = AssistantMessageChunk(tool_calls=[ToolCall(id="", arguments='":1}', index=0)])

    first = base + delta
    second = base + delta

    assert first.tool_calls[0].arguments == '{"a":1}'
    assert second.tool_calls[0].arguments == '{"a":1}'
    assert base.tool_calls[0].arguments == '{"a'


def test_add_accumulates_multiple_argument_deltas():
    start = AssistantMessageChunk(tool_calls=[ToolCall(id="call_1", name="get_weather", arguments="", index=0)])
    d1 = AssistantMessageChunk(tool_calls=[ToolCall(id="", arguments='{"ci', index=0)])
    d2 = AssistantMessageChunk(tool_calls=[ToolCall(id="", arguments='ty": "Beijing"}', index=0)])

    merged = start + d1 + d2

    assert merged.tool_calls[0].arguments == '{"city": "Beijing"}'
    assert start.tool_calls[0].arguments == ""


def test_add_appends_new_tool_call_without_touching_existing_list():
    base = AssistantMessageChunk(tool_calls=[ToolCall(id="call_1", name="a", arguments="{}", index=0)])
    delta = AssistantMessageChunk(tool_calls=[ToolCall(id="call_2", name="b", arguments='{"x', index=1)])

    merged = base + delta

    assert len(merged.tool_calls) == 2
    assert merged.tool_calls[0].arguments == "{}"
    assert merged.tool_calls[1].id == "call_2"
    assert merged.tool_calls[1].arguments == '{"x'
    assert len(base.tool_calls) == 1


def test_add_delta_without_tool_calls_returns_copy_not_alias():
    base = AssistantMessageChunk(tool_calls=[ToolCall(id="call_1", name="a", arguments="{}", index=0)])
    delta = AssistantMessageChunk(content="hi")

    merged = base + delta

    assert merged.tool_calls is not None
    assert merged.tool_calls[0].arguments == "{}"
    assert merged.tool_calls[0] is not base.tool_calls[0]


# -------- 意见 19: usage_metadata 字段级合并 --------

def test_add_merges_usage_metadata_field_by_field():
    chunk1 = AssistantMessageChunk(usage_metadata=UsageMetadata(input_tokens=1000, cache_read_tokens=500))
    chunk2 = AssistantMessageChunk(usage_metadata=UsageMetadata(output_tokens=42))

    merged = chunk1 + chunk2

    assert merged.usage_metadata is not None
    assert merged.usage_metadata.input_tokens == 1000
    assert merged.usage_metadata.output_tokens == 42
    assert merged.usage_metadata.cache_read_tokens == 500
    assert merged.usage_metadata.total_tokens == 1042


def test_add_usage_metadata_anthropic_start_plus_delta():
    """Anthropic 模式：message_start 带 input/cache 统计，message_delta 只带最终 output_tokens。

    适配层对 message_delta 未上报的数值字段统一填 0（见
    anthropic_provider._usage_to_openai_usage），合并不得让这些 0
    覆盖 message_start 已有的统计。
    """
    start = UsageMetadata(
        input_tokens=1600,
        output_tokens=1,
        total_tokens=1601,
        cache_tokens=500,
        cache_read_tokens=500,
        cache_miss_tokens=1100,
        cache_write_tokens=100,
        cache_creation_input_tokens=100,
        cache_status="observed",
        cache_source="provider_usage",
        cache_authoritative=True,
    )
    delta = UsageMetadata(
        output_tokens=42,
        cache_read_tokens=0,  # 适配层对缺失字段的 0 填充
        cache_write_tokens=0,
        cache_status="observed",
        cache_source="provider_usage",
        cache_authoritative=True,
    )

    merged = AssistantMessageChunk(usage_metadata=start) + AssistantMessageChunk(usage_metadata=delta)

    usage = merged.usage_metadata
    assert usage is not None
    assert usage.input_tokens == 1600
    assert usage.output_tokens == 42
    assert usage.cache_read_tokens == 500
    assert usage.cache_write_tokens == 100
    assert usage.cache_creation_input_tokens == 100
    assert usage.cache_tokens == 500
    assert usage.cache_status == "observed"
    assert usage.cache_source == "provider_usage"
    assert usage.cache_authoritative is True
    # total 为派生字段，按合并后的 input+output 重算
    assert usage.total_tokens == 1600 + 42


def test_add_usage_metadata_newest_snapshot_wins():
    """output_tokens 随流上报（逐 delta 快照）的 provider：较新值覆盖旧值。"""
    chunk1 = AssistantMessageChunk(usage_metadata=UsageMetadata(output_tokens=10))
    chunk2 = AssistantMessageChunk(usage_metadata=UsageMetadata(output_tokens=42))

    merged = chunk1 + chunk2

    assert merged.usage_metadata is not None
    assert merged.usage_metadata.output_tokens == 42


def test_add_usage_metadata_zero_cache_stays_zero():
    """两端都真实上报 cache_read=0（从未命中缓存）时保持 0，不被误替换。"""
    chunk1 = AssistantMessageChunk(usage_metadata=UsageMetadata(input_tokens=10, cache_read_tokens=0))
    chunk2 = AssistantMessageChunk(usage_metadata=UsageMetadata(output_tokens=5, cache_read_tokens=0))

    merged = chunk1 + chunk2

    assert merged.usage_metadata is not None
    assert merged.usage_metadata.cache_read_tokens == 0


def test_add_usage_metadata_single_sided():
    usage = UsageMetadata(output_tokens=42)
    chunk_with = AssistantMessageChunk(usage_metadata=usage)
    chunk_without = AssistantMessageChunk(content="hi")

    assert (chunk_without + chunk_with).usage_metadata == usage
    assert (chunk_with + chunk_without).usage_metadata == usage
    assert (chunk_without + AssistantMessageChunk()).usage_metadata is None


def test_add_usage_metadata_does_not_mutate_operands():
    u1 = UsageMetadata(input_tokens=1000, cache_read_tokens=500)
    u2 = UsageMetadata(output_tokens=42)

    merged = AssistantMessageChunk(usage_metadata=u1) + AssistantMessageChunk(usage_metadata=u2)

    assert u1.input_tokens == 1000
    assert u1.output_tokens == 0
    assert u1.cache_read_tokens == 500
    assert u2.input_tokens == 0
    assert u2.output_tokens == 42
    assert merged.usage_metadata is not u1
    assert merged.usage_metadata is not u2
