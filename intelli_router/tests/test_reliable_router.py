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
from intelli_router.observability.events import RoutingEventType


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


class EventCapture:
    """Captures routing events emitted by ReliableRouter."""

    def __init__(self):
        self.events = []

    async def handle_event(self, event):
        self.events.append(event)

    @property
    def name(self):
        return "EventCapture"


def attach_mock_transport(router: ReliableRouter, capture: RequestCapture) -> None:
    """Replace router's httpx client with mock transport (all verify groups)."""
    client = AsyncClient(
        transport=MockTransport(capture.handler),
        timeout=5.0,
    )
    router._clients = {True: client, False: client}
    router._client = client


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class TestReliableRouterInvoke:
    """ReliableRouter.invoke() basic functionality."""

    def test_invoke_returns_assistant_message(self):
        capture = RequestCapture()
        events = EventCapture()
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
            model_id="model-openai-1",
        )
        async def runner():
            router = ReliableRouter(deployments=[dep], model_group_id="group-1")
            router.event_bus.register(events)
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
        assert not hasattr(result, "metadata")
        success_events = [
            event for event in events.events
            if event.event_type == RoutingEventType.REQUEST_SUCCEEDED
        ]
        assert len(success_events) == 1
        assert success_events[0].extra == {
            "model_group_id": "group-1",
            "route_id": "openai-1",
            "model_id": "model-openai-1",
            "model_name": "gpt-4o-mini",
            "provider": "openai",
            "attempt": 1,
        }

    def test_completion_response_includes_deployment_id(self):
        """PR !315 检视意见: completion 响应 dict 附带 deployment_id，
        标识实际服务该请求的部署（invoke() 解析失败告警依赖此字段定位）。"""
        capture = RequestCapture()
        dep = Deployment(
            id="openai-7", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.completion(
                "gpt-4o-mini", [{"role": "user", "content": "hi"}]
            )
            await router.close()
            return r
        result = asyncio.run(runner())

        assert isinstance(result, dict)
        assert result["deployment_id"] == "openai-7"
        # provider 原生字段不受影响
        assert result["choices"][0]["message"]["content"] == "Hello!"

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

    def test_invoke_parser_failure_logs_warning_and_returns_raw_content(self, caplog):
        """意见11: 解析器抛异常时不再静默吞掉——记录 warning（含解析器类名
        与异常栈），同时降级行为保留（调用方拿到原始 content）。"""
        import logging

        from intelli_router.parser.base import BaseOutputParser

        class ExplodingParser(BaseOutputParser):
            """总是失败的解析器。"""

            async def parse(self, inputs):
                raise ValueError("invalid JSON payload")

            async def stream_parse(self, streaming_inputs):
                raise ValueError("invalid JSON payload")  # pragma: no cover

        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        capture = RequestCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(
                messages=[{"role": "user", "content": "hi"}],
                output_parser=ExplodingParser(),
            )
            await router.close()
            return r

        with caplog.at_level(
            logging.WARNING, logger="intelli_router.router.reliable_router"
        ):
            result = asyncio.run(runner())

        # 降级行为保留：原始 content 原样返回
        assert isinstance(result, AssistantMessage)
        assert "Hello!" in result.content

        # warning 已记录，含解析器类名与异常栈（exc_info）
        warnings = [
            rec for rec in caplog.records
            if rec.levelno == logging.WARNING
            and "output_parser" in rec.message
            and "ExplodingParser" in rec.message
            and "returning raw content" in rec.message
        ]
        assert len(warnings) == 1, [r.message for r in caplog.records]
        assert warnings[0].exc_info is not None
        assert isinstance(warnings[0].exc_info[1], ValueError)

    def test_invoke_parser_failure_warning_names_real_deployment(self, caplog):
        """PR !315 检视意见: 解析失败 warning 中的 deployment 应为真实
        deployment id（响应附带 deployment_id 后不再恒为 None）。"""
        import logging

        from intelli_router.parser.base import BaseOutputParser

        class ExplodingParser(BaseOutputParser):
            """总是失败的解析器。"""

            async def parse(self, inputs):
                raise ValueError("invalid JSON payload")

            async def stream_parse(self, streaming_inputs):
                raise ValueError("invalid JSON payload")  # pragma: no cover

        dep = Deployment(
            id="openai-42", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        capture = RequestCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(
                messages=[{"role": "user", "content": "hi"}],
                output_parser=ExplodingParser(),
            )
            await router.close()
            return r

        with caplog.at_level(
            logging.WARNING, logger="intelli_router.router.reliable_router"
        ):
            result = asyncio.run(runner())

        # 降级行为保留：原始 content 原样返回
        assert "Hello!" in result.content

        warnings = [
            rec for rec in caplog.records
            if rec.levelno == logging.WARNING
            and "output_parser" in rec.message
            and "returning raw content" in rec.message
        ]
        assert len(warnings) == 1, [r.message for r in caplog.records]
        # warning 文本包含实际服务的部署 id，而非 "None"
        assert "deployment openai-42" in warnings[0].message
        assert "deployment None" not in warnings[0].message

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

    def test_deployment_request_defaults_are_overridden_by_explicit_params(self):
        class ParamCapture(RequestCapture):
            def _respond(self, request):
                body = json.loads(request.content)
                assert body.get("temperature") == 0.2
                assert body.get("top_p") == 0.7
                assert body.get("max_tokens") == 256
                return Response(200, json={
                    "id": "chatcmpl-test",
                    "choices": [{"index": 0, "message": {"content": "ok"}}],
                    "usage": {"prompt_tokens": 5, "completion_tokens": 2, "total_tokens": 7},
                })

        dep = Deployment(
            id="openai-1",
            model_name="gpt-4o-mini",
            api_key="sk-test",
            api_base="http://test",
            provider="openai",
            request_defaults={"temperature": 0.9, "top_p": 0.7, "max_tokens": 256},
        )
        capture = ParamCapture()

        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            await router.invoke(
                messages=[{"role": "user", "content": "hi"}],
                temperature=0.2,
            )
            await router.close()

        asyncio.run(runner())

    def test_deployment_request_defaults_reject_reserved_keys(self):
        dep = Deployment(
            id="openai-1",
            model_name="gpt-4o-mini",
            api_key="sk-test",
            api_base="http://test",
            provider="openai",
            request_defaults={"model": "bad-model"},
        )
        capture = RequestCapture()

        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            with pytest.raises(ValueError, match="reserved keys"):
                await router.invoke(messages=[{"role": "user", "content": "hi"}])
            await router.close()

        asyncio.run(runner())
        assert capture.requests == []


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
            router = ReliableRouter(deployments=[dep], model_group_id="group-1")
            attach_mock_transport(router, capture)
            chunks = []
            async for ch in router.stream(messages=[{"role": "user", "content": "hi"}]):
                chunks.append(ch)
            await router.close()
            return chunks
        result = asyncio.run(runner())

        assert len(result) >= 1
        assert isinstance(result[0], AssistantMessageChunk)
        assert not hasattr(result[0], "metadata")
        texts = "".join(ch.content for ch in result)
        assert "Hello" in texts
        assert "world" in texts

    def test_stream_records_ttft_not_total_duration(self):
        """stream() 成功回调记录 ttft（首 chunk 耗时），而非整条流总时长（与 stream_completion 口径一致）。"""
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
                # 消费端放慢节奏：拉长流的总时长，但不影响 ttft（首个 chunk 到达时间）
                await asyncio.sleep(0.05)
            await router.close()
            return chunks, router
        result, router = asyncio.run(runner())

        assert len(result) >= 1
        # 两个 chunk × 0.05s sleep，若误记总时长会 >= 0.1；ttft 应显著小于总时长
        recorded = router.state.get_average_latency_raw("openai-1")
        assert recorded is not None
        assert recorded < 0.05, f"expected ttft, got total duration: {recorded}"

    def test_stream_empty_response_records_zero_latency(self):
        """空流（无可解析 chunk）成功时延迟兜底为 0.0 而非 None。"""
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
            return chunks, router
        result, router = asyncio.run(runner())

        assert len(result) == 0
        assert router.state.get_average_latency_raw("openai-1") == 0.0

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
