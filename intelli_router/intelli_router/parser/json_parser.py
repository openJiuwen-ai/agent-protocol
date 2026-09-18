"""JSON 输出解析器。

从 LLM 响应中提取 JSON 数据，支持：
- 裸 JSON（如 {"key": "value"}）
- ```json 代码块包裹的 JSON
- 流式增量解析
"""
import json
import re
from typing import Any, AsyncIterator, Optional, Union

from .base import BaseOutputParser
from ..types import AssistantMessage, AssistantMessageChunk


# Regex to extract content from ```json ... ``` code blocks
_JSON_CODE_BLOCK_RE = re.compile(r"```json\n(.*?)```", re.DOTALL)


class JsonOutputParser(BaseOutputParser):
    """JSON 输出解析器。"""

    async def parse(self, llm_output: Union[str, AssistantMessage]) -> Any:
        """解析 LLM 输出中的 JSON。

        Args:
            llm_output: AssistantMessage 或 content 字符串

        Returns:
            解析后的 JSON 数据，解析失败返回 None
        """
        text = self._extract_content(llm_output)
        if not text:
            return None

        # 尝试提取 ```json 代码块
        match = _JSON_CODE_BLOCK_RE.search(text)
        if match:
            json_str = match.group(1).strip()
        else:
            json_str = text.strip()

        try:
            return json.loads(json_str)
        except json.JSONDecodeError:
            return None

    async def stream_parse(
        self, streaming_inputs: AsyncIterator
    ) -> AsyncIterator[Optional[dict]]:
        """流式解析 JSON。

        维护一个 buffer，每收到一个 chunk 尝试解析。
        解析成功后 yield 结果并清空 buffer。

        Args:
            streaming_inputs: AsyncIterator[AssistantMessageChunk]

        Yields:
            解析出的 dict，或 None
        """
        buffer = ""

        async for chunk in streaming_inputs:
            if isinstance(chunk, AssistantMessageChunk) and chunk.content:
                buffer += chunk.content

            # 尝试解析当前 buffer
            match = _JSON_CODE_BLOCK_RE.search(buffer)
            if match:
                json_str = match.group(1).strip()
                try:
                    parsed = json.loads(json_str)
                    yield parsed
                    buffer = buffer[match.end():].strip()
                    continue
                except json.JSONDecodeError:
                    pass

            # 也尝试解析裸 JSON
            stripped = buffer.strip()
            if stripped.startswith("{") and stripped.endswith("}"):
                try:
                    parsed = json.loads(stripped)
                    yield parsed
                    buffer = ""
                except json.JSONDecodeError:
                    pass

        # 处理剩余的 buffer
        if buffer.strip():
            match = _JSON_CODE_BLOCK_RE.search(buffer)
            if match:
                json_str = match.group(1).strip()
            else:
                json_str = buffer.strip()
            try:
                yield json.loads(json_str)
            except json.JSONDecodeError:
                pass
