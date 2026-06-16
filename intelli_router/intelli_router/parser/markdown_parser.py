"""Markdown 输出解析器。

将 LLM 输出的 Markdown 文本解析为结构化 MarkdownContent，
包含 headers、code_blocks、links、images、tables、lists 等元素。
"""
import re
from dataclasses import dataclass, field
from typing import Any, AsyncIterator, Dict, List, Optional, Union

from .base import BaseOutputParser
from ..types import AssistantMessage, AssistantMessageChunk


class MarkdownElementType:
    """Markdown 元素类型常量。"""
    HEADER = "header"
    CODE_BLOCK = "code_block"
    INLINE_CODE = "inline_code"
    LINK = "link"
    IMAGE = "image"
    TABLE = "table"
    LIST = "list"
    TEXT = "text"


@dataclass
class MarkdownElement:
    """单个 Markdown 元素。"""
    type: str
    content: Dict[str, Any]
    start_pos: int
    end_pos: int
    raw: str


@dataclass
class MarkdownContent:
    """结构化 Markdown 内容。"""
    raw_content: str = ""
    elements: List[MarkdownElement] = field(default_factory=list)
    headers: List[Dict[str, str]] = field(default_factory=list)
    code_blocks: List[Dict[str, str]] = field(default_factory=list)
    links: List[Dict[str, str]] = field(default_factory=list)
    images: List[Dict[str, str]] = field(default_factory=list)
    tables: List[str] = field(default_factory=list)
    lists: List[str] = field(default_factory=list)


class MarkdownOutputParser(BaseOutputParser):
    """Markdown 输出解析器。"""

    async def parse(self, llm_output: Union[str, AssistantMessage]) -> Optional[MarkdownContent]:
        """解析 Markdown 文本为结构化 MarkdownContent。

        Args:
            llm_output: AssistantMessage 或 content 字符串

        Returns:
            MarkdownContent 或 None（解析失败时）
        """
        text = self._extract_content(llm_output)
        if not text:
            return None

        try:
            md = MarkdownContent(raw_content=text)
            self._extract_all_elements(text, md)
            self._populate_categorized_lists(md)
            return md
        except Exception:
            return None

    async def stream_parse(
        self, streaming_inputs: AsyncIterator
    ) -> AsyncIterator[Optional[MarkdownContent]]:
        """流式解析 Markdown。

        Batches chunks and only reparses at intervals to avoid O(n²) behavior.

        Args:
            streaming_inputs: AsyncIterator[AssistantMessageChunk]

        Yields:
            MarkdownContent（增量更新）
        """
        buffer = ""
        last_parsed_length = 0
        chunk_count = 0
        # Reparse every N chunks to amortize cost
        REPARSE_INTERVAL = 10

        async for chunk in streaming_inputs:
            if isinstance(chunk, AssistantMessageChunk) and chunk.content:
                buffer += chunk.content
                chunk_count += 1

            if len(buffer) > last_parsed_length and chunk_count >= REPARSE_INTERVAL:
                try:
                    md = MarkdownContent(raw_content=buffer)
                    self._extract_all_elements(buffer, md)
                    self._populate_categorized_lists(md)
                    yield md
                    last_parsed_length = len(buffer)
                    chunk_count = 0
                except Exception:
                    continue

        # Final parse of complete buffer
        if len(buffer) > last_parsed_length and buffer.strip():
            try:
                md = MarkdownContent(raw_content=buffer)
                self._extract_all_elements(buffer, md)
                self._populate_categorized_lists(md)
                yield md
            except Exception:
                pass

    # ------------------------------------------------------------------
    # 内部解析方法
    # ------------------------------------------------------------------

    def _extract_all_elements(self, text: str, md: MarkdownContent) -> None:
        """提取所有 Markdown 元素并排序。"""
        elements = []

        # Headers
        for m in re.finditer(r'^(#{1,6})\s+(.+)$', text, re.MULTILINE):
            elements.append(MarkdownElement(
                type=MarkdownElementType.HEADER,
                content={"level": str(len(m.group(1))), "title": m.group(2).strip()},
                start_pos=m.start(), end_pos=m.end(), raw=m.group(0),
            ))

        # Code blocks
        for m in re.finditer(r'```(\w*)\n(.*?)\n```', text, re.DOTALL):
            elements.append(MarkdownElement(
                type=MarkdownElementType.CODE_BLOCK,
                content={"language": m.group(1) or "text", "code": m.group(2)},
                start_pos=m.start(), end_pos=m.end(), raw=m.group(0),
            ))

        # Inline code
        for m in re.finditer(r'`([^`\n]+)`', text):
            elements.append(MarkdownElement(
                type=MarkdownElementType.INLINE_CODE,
                content={"code": m.group(1)},
                start_pos=m.start(), end_pos=m.end(), raw=m.group(0),
            ))

        # Images
        for m in re.finditer(r'!\[([^\]]*)\]\(([^)]+)\)', text):
            elements.append(MarkdownElement(
                type=MarkdownElementType.IMAGE,
                content={"alt": m.group(1), "url": m.group(2)},
                start_pos=m.start(), end_pos=m.end(), raw=m.group(0),
            ))

        # Links (not images)
        for m in re.finditer(r'(?<!\!)\[([^\]]+)\]\(([^)]+)\)', text):
            elements.append(MarkdownElement(
                type=MarkdownElementType.LINK,
                content={"text": m.group(1), "url": m.group(2)},
                start_pos=m.start(), end_pos=m.end(), raw=m.group(0),
            ))

        # Tables and lists (multiline)
        self._extract_multiline_elements(text, elements)

        elements.sort(key=lambda x: x.start_pos)
        md.elements = elements

    def _extract_multiline_elements(self, text: str, elements: List) -> None:
        """提取多行元素（表格、列表）。"""
        lines = text.split("\n")
        current_pos = 0

        table_lines = []
        list_lines = []
        table_start = -1
        list_start = -1

        for i, line in enumerate(lines):
            line_start = current_pos
            line_end = current_pos + len(line)
            current_pos = line_end + 1

            # Table detection
            if "|" in line.strip() and line.strip():
                if not table_lines:
                    table_start = line_start
                table_lines.append(line)
            else:
                if table_lines:
                    content = "\n".join(table_lines)
                    elements.append(MarkdownElement(
                        type=MarkdownElementType.TABLE,
                        content={"table": content},
                        start_pos=table_start, end_pos=line_start - 1, raw=content,
                    ))
                    table_lines = []

            # List detection
            if re.match(r'^\s*[-*+]\s+', line) or re.match(r'^\s*\d+\.\s+', line):
                if not list_lines:
                    list_start = line_start
                list_lines.append(line)
            elif re.match(r'^\s*$', line) and list_lines:
                list_lines.append(line)
            else:
                if list_lines:
                    content = "\n".join(list_lines).strip()
                    if content:
                        elements.append(MarkdownElement(
                            type=MarkdownElementType.LIST,
                            content={"list": content},
                            start_pos=list_start, end_pos=line_start - 1, raw=content,
                        ))
                    list_lines = []

        # Flush remaining
        if table_lines:
            content = "\n".join(table_lines)
            elements.append(MarkdownElement(
                type=MarkdownElementType.TABLE,
                content={"table": content},
                start_pos=table_start, end_pos=len(text), raw=content,
            ))
        if list_lines:
            content = "\n".join(list_lines).strip()
            if content:
                elements.append(MarkdownElement(
                    type=MarkdownElementType.LIST,
                    content={"list": content},
                    start_pos=list_start, end_pos=len(text), raw=content,
                ))

    def _populate_categorized_lists(self, md: MarkdownContent) -> None:
        """按类型填充分类列表。"""
        for el in md.elements:
            if el.type == MarkdownElementType.HEADER:
                md.headers.append({"level": el.content["level"], "title": el.content["title"], "raw": el.raw})
            elif el.type == MarkdownElementType.CODE_BLOCK:
                md.code_blocks.append({"language": el.content["language"], "code": el.content["code"], "raw": el.raw})
            elif el.type == MarkdownElementType.INLINE_CODE:
                md.code_blocks.append({"language": "inline", "code": el.content["code"], "raw": el.raw})
            elif el.type == MarkdownElementType.LINK:
                md.links.append({"text": el.content["text"], "url": el.content["url"], "raw": el.raw})
            elif el.type == MarkdownElementType.IMAGE:
                md.images.append({"alt": el.content["alt"], "url": el.content["url"], "raw": el.raw})
            elif el.type == MarkdownElementType.TABLE:
                md.tables.append(el.content["table"])
            elif el.type == MarkdownElementType.LIST:
                md.lists.append(el.content["list"])
