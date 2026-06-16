"""类型化响应对象。

所有 provider 的响应统一转换为这些类型，供上层调用者使用。
"""
from dataclasses import dataclass, field
from typing import List, Optional


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
    """Token 用量和费用信息。"""
    model_name: Optional[str] = None
    input_tokens: int = 0
    output_tokens: int = 0
    total_tokens: int = 0
    cache_tokens: int = 0

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


@dataclass
class AssistantMessageChunk:
    """流式响应 chunk。"""
    content: str = ""
    reasoning_content: Optional[str] = None
    tool_calls: Optional[List[ToolCall]] = None
    usage_metadata: Optional[UsageMetadata] = None
    finish_reason: Optional[str] = None

    def __add__(self, other: "AssistantMessageChunk") -> "AssistantMessageChunk":
        """拼接两个 chunk（用于流式累积）。"""
        merged_tool_calls = self._merge_tool_calls(self.tool_calls, other.tool_calls)
        return AssistantMessageChunk(
            content=self.content + (other.content or ""),
            reasoning_content=(self.reasoning_content or "") + (other.reasoning_content or "") if (self.reasoning_content is not None or other.reasoning_content is not None) else None,
            tool_calls=merged_tool_calls,
            usage_metadata=other.usage_metadata if other.usage_metadata is not None else self.usage_metadata,
            finish_reason=other.finish_reason if other.finish_reason is not None else self.finish_reason,
        )

    @staticmethod
    def _merge_tool_calls(
        existing: Optional[List["ToolCall"]],
        incoming: Optional[List["ToolCall"]],
    ) -> Optional[List["ToolCall"]]:
        """按 index 合并 tool_calls，拼接 arguments 字符串。"""
        if incoming is None:
            return existing
        if existing is None:
            return [ToolCall(id=tc.id, type=tc.type, name=tc.name, arguments=tc.arguments, index=tc.index) for tc in incoming]

        # Build index map from existing
        by_index: dict = {tc.index: tc for tc in existing if tc.index is not None}
        result = list(existing)

        for tc in incoming:
            idx = tc.index
            if idx is not None and idx in by_index:
                # Merge into existing entry
                target = by_index[idx]
                if tc.id:
                    target.id = tc.id
                if tc.name:
                    target.name = tc.name
                target.arguments += tc.arguments
            else:
                # New tool call
                new_tc = ToolCall(id=tc.id, type=tc.type, name=tc.name, arguments=tc.arguments, index=tc.index)
                result.append(new_tc)
                if idx is not None:
                    by_index[idx] = new_tc

        return result


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
