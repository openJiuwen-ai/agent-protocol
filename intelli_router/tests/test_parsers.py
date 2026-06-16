"""输出解析器单元测试。"""
import pytest
from intelli_router.parser import JsonOutputParser, MarkdownOutputParser
from intelli_router.types import AssistantMessage, AssistantMessageChunk


class TestJsonOutputParser:
    """JsonOutputParser 测试。"""

    @pytest.mark.asyncio
    async def test_parse_bare_json(self):
        parser = JsonOutputParser()
        result = await parser.parse('{"key": "value", "num": 42}')
        assert result == {"key": "value", "num": 42}

    @pytest.mark.asyncio
    async def test_parse_json_codeblock(self):
        parser = JsonOutputParser()
        text = "Some text\n```json\n{\"name\": \"test\"}\n```\nmore text"
        result = await parser.parse(text)
        assert result == {"name": "test"}

    @pytest.mark.asyncio
    async def test_parse_from_assistant_message(self):
        parser = JsonOutputParser()
        msg = AssistantMessage(content='{"hello": "world"}')
        result = await parser.parse(msg)
        assert result == {"hello": "world"}

    @pytest.mark.asyncio
    async def test_parse_invalid_json(self):
        parser = JsonOutputParser()
        result = await parser.parse("not json")
        assert result is None

    @pytest.mark.asyncio
    async def test_parse_empty(self):
        parser = JsonOutputParser()
        assert await parser.parse("") is None
        assert await parser.parse(None) is None

    @pytest.mark.asyncio
    async def test_stream_parse_incremental(self):
        parser = JsonOutputParser()

        async def stream():
            yield AssistantMessageChunk(content='{"key": "val')
            yield AssistantMessageChunk(content='ue"}')

        results = []
        async for r in parser.stream_parse(stream()):
            results.append(r)

        assert len(results) >= 1
        assert results[-1] == {"key": "value"}


class TestMarkdownOutputParser:
    """MarkdownOutputParser 测试。"""

    @pytest.mark.asyncio
    async def test_parse_headers(self):
        parser = MarkdownOutputParser()
        md = await parser.parse("# Title\n\n## Section")
        assert md is not None
        assert len(md.headers) == 2
        assert md.headers[0]["level"] == "1"
        assert md.headers[0]["title"] == "Title"

    @pytest.mark.asyncio
    async def test_parse_code_block(self):
        parser = MarkdownOutputParser()
        md = await parser.parse("```python\nprint('hello')\n```")
        assert md is not None
        assert len(md.code_blocks) == 1
        assert md.code_blocks[0]["language"] == "python"
        assert "print" in md.code_blocks[0]["code"]

    @pytest.mark.asyncio
    async def test_parse_links_and_images(self):
        parser = MarkdownOutputParser()
        md = await parser.parse("[link](http://x.com) ![img](http://y.png)")
        assert md is not None
        assert len(md.links) == 1
        assert md.links[0]["url"] == "http://x.com"
        assert len(md.images) == 1
        assert md.images[0]["url"] == "http://y.png"

    @pytest.mark.asyncio
    async def test_parse_empty(self):
        parser = MarkdownOutputParser()
        assert await parser.parse("") is None
        assert await parser.parse(None) is None

    @pytest.mark.asyncio
    async def test_parse_from_assistant_message(self):
        parser = MarkdownOutputParser()
        msg = AssistantMessage(content="# Hello")
        md = await parser.parse(msg)
        assert md is not None
        assert len(md.headers) == 1
