"""输出解析器基类。"""
from typing import Any, AsyncIterator, Optional, Union
from abc import ABC, abstractmethod

from ..types import AssistantMessage, AssistantMessageChunk


class BaseOutputParser(ABC):
    """LLM 输出解析器基类。

    parse()         — 解析非流式响应（AssistantMessage 或 content 字符串）
    stream_parse()  — 解析流式响应（逐 chunk 增量解析）
    """

    @staticmethod
    def _extract_content(llm_output: Union[str, AssistantMessage]) -> Optional[str]:
        """从 AssistantMessage 或字符串中提取纯文本内容。"""
        if isinstance(llm_output, AssistantMessage):
            return llm_output.content or None
        if isinstance(llm_output, str):
            return llm_output
        return None

    @abstractmethod
    async def parse(self, inputs: Any) -> Any:
        """解析 LLM 输出。

        Args:
            inputs: AssistantMessage 或 content 字符串

        Returns:
            解析后的结构化数据
        """
        raise NotImplementedError()

    @abstractmethod
    async def stream_parse(
        self, streaming_inputs: AsyncIterator
    ) -> AsyncIterator[Any]:
        """解析流式 LLM 输出。

        Args:
            streaming_inputs: AsyncIterator[AssistantMessageChunk]

        Yields:
            逐段解析的结构化数据
        """
        raise NotImplementedError()
