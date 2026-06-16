"""ReliableRouter invoke/stream 系统测试 — 使用 MockTransport 验证完整 pipeline。"""
import asyncio
import json

import pytest

from httpx import Request, Response, AsyncClient
from httpx._transports.mock import MockTransport

from intelli_router import (
    ReliableRouter,
    AssistantMessage,
    AssistantMessageChunk,
    Deployment,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


class RequestCapture:
    """Captures all requests made through MockTransport."""
    def __init__(self):
        self.requests = []

    def handler(self, request: Request) -> Response:
        self.requests.append(request)
        return self._respond(request)

    def _respond(self, request: Request) -> Response:
        """Default response — override by subclassing."""
        return Response(200, json={
            "id": "chatcmpl-test",
            "choices": [{
                "index": 0,
                "message": {"role": "assistant", "content": "Hello!"},
                "finish_reason": "stop",
            }],
            "usage": {
                "prompt_tokens": 10,
                "completion_tokens": 20,
                "total_tokens": 30,
            },
        })


def attach_mock_transport(router: ReliableRouter, capture: RequestCapture) -> None:
    """Replace router's httpx client with mock transport."""
    router._client = AsyncClient(
        transport=MockTransport(capture.handler),
        timeout=5.0,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class TestReliableRouterInvoke:
    """ReliableRouter.invoke() basic functionality."""

    def test_invoke_returns_assistant_message(self):
        capture = RequestCapture()
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(messages=[{"role": "user", "content": "hi"}])
            await router.close()
            return r
        result = asyncio.run(runner())

        assert isinstance(result, AssistantMessage), type(result)
        assert "Hello!" in result.content
        assert result.finish_reason == "stop"
        assert result.usage_metadata is not None
        assert result.usage_metadata.input_tokens == 10
        assert result.usage_metadata.output_tokens == 20

    def test_invoke_with_tool_calls(self):
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class ToolCallCapture(RequestCapture):
            def _respond(self, request):
                return Response(200, json={
                    "id": "chatcmpl-tool",
                    "choices": [{
                        "index": 0,
                        "message": {
                            "role": "assistant",
                            "content": None,
                            "tool_calls": [{
                                "id": "call_1",
                                "type": "function",
                                "function": {
                                    "name": "get_weather",
                                    "arguments": '{"loc": "Beijing"}',
                                },
                            }],
                        },
                        "finish_reason": "tool_calls",
                    }],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15},
                })

        capture = ToolCallCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(
                messages=[{"role": "user", "content": "weather?"}],
                tools=[{"type": "function", "function": {"name": "get_weather"}}],
            )
            await router.close()
            return r
        result = asyncio.run(runner())

        assert isinstance(result, AssistantMessage)
        assert result.finish_reason == "tool_calls"
        assert result.tool_calls is not None
        assert len(result.tool_calls) == 1
        assert result.tool_calls[0].name == "get_weather"
        assert "Beijing" in result.tool_calls[0].arguments

    def test_invoke_passes_request_params(self):
        class ParamCapture(RequestCapture):
            def _respond(self, request):
                body = json.loads(request.content)
                assert body.get("temperature") == 0.5
                assert body.get("max_tokens") == 100
                return Response(200, json={
                    "id": "chatcmpl-test",
                    "choices": [{"index": 0, "message": {"content": "ok"}}],
                    "usage": {"prompt_tokens": 5, "completion_tokens": 2, "total_tokens": 7},
                })

        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        capture = ParamCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(
                messages=[{"role": "user", "content": "hi"}],
                temperature=0.5,
                max_tokens=100,
            )
            await router.close()
            return r
        asyncio.run(runner())


class TestReliableRouterStream:
    """ReliableRouter.stream() basic functionality."""

    def test_stream_returns_chunks(self):
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class StreamCapture(RequestCapture):
            def _respond(self, request):
                content = "data: " + json.dumps({
                    "choices": [{"index": 0, "delta": {"content": "Hello"}, "finish_reason": None}]
                }) + "\n\n"
                content += "data: " + json.dumps({
                    "choices": [{"index": 0, "delta": {"content": " world"}, "finish_reason": "stop"}]
                }) + "\n\n"
                content += "data: [DONE]\n\n"
                return Response(200, text=content)

        capture = StreamCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            chunks = []
            async for ch in router.stream(messages=[{"role": "user", "content": "hi"}]):
                chunks.append(ch)
            await router.close()
            return chunks
        result = asyncio.run(runner())

        assert len(result) >= 1
        assert isinstance(result[0], AssistantMessageChunk)
        texts = "".join(ch.content for ch in result)
        assert "Hello" in texts
        assert "world" in texts

    def test_stream_empty_response(self):
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class EmptyCapture(RequestCapture):
            def _respond(self, request):
                return Response(200, text="data: [DONE]\n\n")

        capture = EmptyCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            chunks = []
            async for ch in router.stream(messages=[{"role": "user", "content": "hi"}]):
                chunks.append(ch)
            await router.close()
            return chunks
        result = asyncio.run(runner())
        assert len(result) == 0


class TestReliableRouterDeepSeek:
    """DeepSeek provider adapter integration."""

    def test_deepseek_adapter_adds_reasoning_content(self):
        dep = Deployment(
            id="deepseek-1", model_name="deepseek-chat",
            api_key="sk-test", api_base="http://test", provider="deepseek",
        )

        class DeepSeekCapture(RequestCapture):
            def _respond(self, request):
                body = json.loads(request.content)
                for msg in body["messages"]:
                    if msg["role"] == "assistant" and "reasoning_content" not in msg:
                        assert False, f"Missing reasoning_content in {msg}"
                return Response(200, json={
                    "id": "chatcmpl-test",
                    "choices": [{"index": 0, "message": {"content": "ok"}}],
                    "usage": {"prompt_tokens": 5, "completion_tokens": 2, "total_tokens": 7},
                })

        capture = DeepSeekCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(
                messages=[
                    {"role": "user", "content": "hi"},
                    {"role": "assistant", "content": "previous response"},
                ],
            )
            await router.close()
            return r
        asyncio.run(runner())


class TestReliableRouterErrors:
    """Error handling."""

    def test_invoke_on_closed_router(self):
        dep = Deployment(
            id="test", model_name="test",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        async def runner():
            router = ReliableRouter(deployments=[dep])
            # close() should not raise — httpx client cleanup is safe
            await router.close()
            return router
        router = asyncio.run(runner())
        assert router._client is None, "httpx client should be None after close"
