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

    # ---- PR #358 review fix: ```json 代码块变体 ----

    @pytest.mark.asyncio
    async def test_parse_json_codeblock_same_line(self):
        """```json 标记后不换行（同行）也能提取。"""
        parser = JsonOutputParser()
        result = await parser.parse('```json {"a": 1}```')
        assert result == {"a": 1}

    @pytest.mark.asyncio
    async def test_parse_json_codeblock_uppercase_marker(self):
        """```JSON 大写标记也能提取（大小写不敏感）。"""
        parser = JsonOutputParser()
        result = await parser.parse('```JSON\n{"a": 1}\n```')
        assert result == {"a": 1}

    @pytest.mark.asyncio
    async def test_parse_bare_codeblock_without_marker(self):
        """无语言标记的裸 ``` 块也能提取。"""
        parser = JsonOutputParser()
        result = await parser.parse('```\n{"a": 1}\n```')
        assert result == {"a": 1}

    @pytest.mark.asyncio
    async def test_parse_json_codeblock_with_surrounding_text(self):
        """经典 ```json 块（含前后文本）行为不变。"""
        parser = JsonOutputParser()
        result = await parser.parse('前缀\n```json\n{"a": 1}\n```\n后缀')
        assert result == {"a": 1}

    @pytest.mark.asyncio
    async def test_parse_python_codeblock_not_json(self):
        """```python 等其他语言标记的块不被当作 JSON 解析成功。"""
        parser = JsonOutputParser()
        assert await parser.parse('```python\nx = 1\n```') is None
        assert await parser.parse('```python\ncode\n```') is None


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

    # ---- PR #358 review fix: 行内 code 与围栏块分离 ----

    @pytest.mark.asyncio
    async def test_parse_inline_code(self):
        """普通行内 `code` 进入 inline_codes，不再混入 code_blocks。"""
        parser = MarkdownOutputParser()
        md = await parser.parse("Use `print()` to output.")
        assert md is not None
        assert len(md.inline_codes) == 1
        assert md.inline_codes[0]["code"] == "print()"
        assert md.code_blocks == []

    @pytest.mark.asyncio
    async def test_parse_backticks_inside_fenced_block_not_inline(self):
        """围栏代码块内部的反引号不计为行内 code。"""
        parser = MarkdownOutputParser()
        md = await parser.parse("```python\nx = `y`\n```")
        assert md is not None
        assert len(md.code_blocks) == 1
        assert md.code_blocks[0]["language"] == "python"
        assert md.code_blocks[0]["code"] == "x = `y`"
        assert md.inline_codes == []

    @pytest.mark.asyncio
    async def test_parse_mixed_inline_and_fenced_code(self):
        """围栏外的行内 code 正常提取，围栏内的不提取。"""
        parser = MarkdownOutputParser()
        md = await parser.parse("Run `cmd` first:\n```python\nx = `y`\n```")
        assert md is not None
        assert len(md.code_blocks) == 1
        assert md.code_blocks[0]["language"] == "python"
        assert len(md.inline_codes) == 1
        assert md.inline_codes[0]["code"] == "cmd"
        assert all(cb["language"] != "inline" for cb in md.code_blocks)
