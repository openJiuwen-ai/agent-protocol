"""类型化响应对象。

所有 provider 的响应统一转换为这些类型，供上层调用者使用。
"""
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


@dataclass
class ToolCall:
    """工具调用信息。"""
    id: str
    type: str = "function"
    name: str = ""
    arguments: str = ""
    index: Optional[int] = None


@dataclass
class UsageMetadata:
    """Token 用量、费用和缓存命中信息。

    Cache fields are normalized from provider responses when available:
    ``cache_tokens``/``cache_read_tokens`` describe reused prompt tokens,
    ``cache_write_tokens``/``cache_creation_input_tokens`` describe newly
    written cache tokens, and ``cache_miss_tokens`` describes prompt tokens
    not served from cache. ``cache_status`` is a free-form status label
    currently populated with values such as ``"observed"`` when the provider
    returned authoritative cache fields. ``cache_source`` identifies where the
    cache values came from, for example ``"provider_usage"``. When
    ``cache_authoritative`` is true, callers may treat the cache fields as
    provider-reported values; false means absent, derived, or best-effort data.
    """
    model_name: Optional[str] = None
    input_tokens: int = 0
    output_tokens: int = 0
    total_tokens: int = 0
    cache_tokens: int = 0
    cache_read_tokens: Optional[int] = None
    cache_miss_tokens: Optional[int] = None
    cache_write_tokens: Optional[int] = None
    cache_status: Optional[str] = None
    cache_source: Optional[str] = None
    cache_authoritative: bool = False
    cache_creation_input_tokens: Optional[int] = None
    reasoning_tokens: int = 0
    input_cost: float = 0.0
    output_cost: float = 0.0
    total_cost: float = 0.0

    def __post_init__(self):
        if self.total_tokens == 0 and (self.input_tokens or self.output_tokens):
            self.total_tokens = self.input_tokens + self.output_tokens


@dataclass
class AssistantMessage:
    """非流式响应消息。"""
    content: str
    tool_calls: Optional[List[ToolCall]] = None
    usage_metadata: Optional[UsageMetadata] = None
    finish_reason: str = "stop"
    reasoning_content: Optional[str] = None
    parser_content: Optional[Any] = None
    prompt_token_ids: Optional[List[int]] = None
    completion_token_ids: Optional[List[int]] = None
    logprobs: Optional[Any] = None
    response_id: Optional[str] = None
    response_model: Optional[str] = None
    provider_metadata: Dict[str, Any] = field(default_factory=dict)
    provider_content: Optional[Any] = None


@dataclass
class AssistantMessageChunk:
    """流式响应 chunk。"""
    content: str = ""
    reasoning_content: Optional[str] = None
    tool_calls: Optional[List[ToolCall]] = None
    usage_metadata: Optional[UsageMetadata] = None
    finish_reason: Optional[str] = None
    parser_content: Optional[Any] = None
    prompt_token_ids: Optional[List[int]] = None
    completion_token_ids: Optional[List[int]] = None
    logprobs: Optional[Any] = None
    response_id: Optional[str] = None
    response_model: Optional[str] = None
    provider_metadata: Dict[str, Any] = field(default_factory=dict)
    provider_content: Optional[Any] = None

    def __add__(self, other: "AssistantMessageChunk") -> "AssistantMessageChunk":
        """拼接两个 chunk（用于流式累积）。"""
        merged_tool_calls = self._merge_tool_calls(self.tool_calls, other.tool_calls)
        return AssistantMessageChunk(
            content=self.content + (other.content or ""),
            reasoning_content=(self.reasoning_content or "") + (other.reasoning_content or "") if (self.reasoning_content is not None or other.reasoning_content is not None) else None,
            tool_calls=merged_tool_calls,
            usage_metadata=AssistantMessageChunk._merge_usage_metadata(self.usage_metadata, other.usage_metadata),
            finish_reason=other.finish_reason if other.finish_reason is not None else self.finish_reason,
            parser_content=other.parser_content if other.parser_content is not None else self.parser_content,
            prompt_token_ids=self.prompt_token_ids or other.prompt_token_ids,
            completion_token_ids=(self.completion_token_ids or []) + (other.completion_token_ids or []) or None,
            logprobs=other.logprobs if other.logprobs is not None else self.logprobs,
            response_id=other.response_id or self.response_id,
            response_model=other.response_model or self.response_model,
            provider_metadata={**self.provider_metadata, **other.provider_metadata},
            provider_content=other.provider_content if other.provider_content is not None else self.provider_content,
        )

    @staticmethod
    def _merge_tool_calls(
        existing: Optional[List["ToolCall"]],
        incoming: Optional[List["ToolCall"]],
    ) -> Optional[List["ToolCall"]]:
        """按 index 合并 tool_calls，拼接 arguments 字符串。

        不修改任何传入的列表和 ToolCall 对象：所有条目（含 existing
        中的）都以新实例返回，保证 ``__add__`` 的操作数在流式累积
        （尤其是重复累加或复用 chunk）时不被改写。
        """
        if existing is None and incoming is None:
            return None
        if incoming is None:
            return [ToolCall(id=tc.id, type=tc.type, name=tc.name, arguments=tc.arguments, index=tc.index) for tc in existing]
        if existing is None:
            return [ToolCall(id=tc.id, type=tc.type, name=tc.name, arguments=tc.arguments, index=tc.index) for tc in incoming]

        # existing 中的 ToolCall 先拷贝成新实例，后续合并只改写拷贝
        by_index: dict = {}
        result: List["ToolCall"] = []
        for tc in existing:
            copied = ToolCall(id=tc.id, type=tc.type, name=tc.name, arguments=tc.arguments, index=tc.index)
            result.append(copied)
            if copied.index is not None:
                by_index[copied.index] = copied

        for tc in incoming:
            idx = tc.index
            if idx is not None and idx in by_index:
                # 合并到已有条目的拷贝上
                target = by_index[idx]
                if tc.id:
                    target.id = tc.id
                if tc.name:
                    target.name = tc.name
                target.arguments += tc.arguments
            else:
                # 新的工具调用
                new_tc = ToolCall(id=tc.id, type=tc.type, name=tc.name, arguments=tc.arguments, index=tc.index)
                result.append(new_tc)
                if idx is not None:
                    by_index[idx] = new_tc

        return result

    @staticmethod
    def _merge_usage_metadata(
        existing: Optional["UsageMetadata"],
        incoming: Optional["UsageMetadata"],
    ) -> Optional["UsageMetadata"]:
        """字段级合并两个 UsageMetadata（用于流式累积）。

        流式场景下不同阶段上报用量的不同字段（如 Anthropic
        message_start 携带 input/cache 统计、message_delta 只携带最终
        output_tokens），整体覆盖会丢失先到的字段，因此逐字段合并，
        且不修改传入的两个 UsageMetadata 对象：

        - 数值字段（token 数 / 费用）：provider 转换层对未上报的字段
          统一填默认值 0（见 anthropic_provider._usage_to_openai_usage
          与 ReliableRouter._usage_to_metadata），因此 0 / None 均视为
          "未上报"：incoming 非零则取 incoming（较新快照，与
          output_tokens 随流递增、最终 chunk 携带完整用量的填充方式
          一致），否则保留 existing。
        - 字符串字段（model_name / cache_status / cache_source）：
          None 视为未上报，取 incoming 非 None 的值，否则保留 existing。
        - cache_authoritative：一端为 True 即保留 True（合并结果的
          cache 字段仍来自 provider 上报）。
        - total_tokens：派生字段，input/output 合并后按两者之和重算
          （与 UsageMetadata.__post_init__ 及各 provider 的填充方式
          一致），两端都未上报 input/output 时取较新非零值。
        """
        if existing is None:
            return incoming
        if incoming is None:
            return existing

        def _newest_number(incoming_value, existing_value):
            if incoming_value is None or incoming_value == 0:
                return existing_value
            return incoming_value

        input_tokens = _newest_number(incoming.input_tokens, existing.input_tokens)
        output_tokens = _newest_number(incoming.output_tokens, existing.output_tokens)

        total_tokens = _newest_number(incoming.total_tokens, existing.total_tokens)
        if input_tokens or output_tokens:
            total_tokens = input_tokens + output_tokens

        return UsageMetadata(
            model_name=incoming.model_name if incoming.model_name is not None else existing.model_name,
            input_tokens=input_tokens,
            output_tokens=output_tokens,
            total_tokens=total_tokens,
            cache_tokens=_newest_number(incoming.cache_tokens, existing.cache_tokens),
            cache_read_tokens=_newest_number(incoming.cache_read_tokens, existing.cache_read_tokens),
            cache_miss_tokens=_newest_number(incoming.cache_miss_tokens, existing.cache_miss_tokens),
            cache_write_tokens=_newest_number(incoming.cache_write_tokens, existing.cache_write_tokens),
            cache_status=incoming.cache_status if incoming.cache_status is not None else existing.cache_status,
            cache_source=incoming.cache_source if incoming.cache_source is not None else existing.cache_source,
            cache_authoritative=existing.cache_authoritative or incoming.cache_authoritative,
            cache_creation_input_tokens=_newest_number(
                incoming.cache_creation_input_tokens, existing.cache_creation_input_tokens
            ),
            reasoning_tokens=_newest_number(incoming.reasoning_tokens, existing.reasoning_tokens),
            input_cost=_newest_number(incoming.input_cost, existing.input_cost),
            output_cost=_newest_number(incoming.output_cost, existing.output_cost),
            total_cost=_newest_number(incoming.total_cost, existing.total_cost),
        )


@dataclass
class ImageGenerationResponse:
    """文生图 / 图生图响应。"""
    model: str
    images: List[str]


@dataclass
class AudioGenerationResponse:
    """语音合成响应。"""
    model: str
    audio_url: Optional[str] = None
    audio_data: Optional[bytes] = None
    format: Optional[str] = None


@dataclass
class VideoGenerationResponse:
    """文生视频 / 图生视频响应。"""
    model: str
    video_url: str
    duration: Optional[int] = None
    resolution: Optional[str] = None
    format: str = "mp4"
