"""ReliableRouter invoke/stream 系统测试 — 使用 MockTransport 验证完整 pipeline。"""
import asyncio
import json

import pytest

from unittest.mock import patch

from httpx import Request, Response, AsyncClient
from httpx._transports.mock import MockTransport

from intelli_router import (
    ReliableRouter,
    AssistantMessage,
    AssistantMessageChunk,
    Deployment,
)
from intelli_router.core.deployment import DeploymentStatus
from intelli_router.observability.events import RoutingEventType
from intelli_router.utils.exceptions import (
    AllDeploymentsFailed,
    DeploymentNetworkError,
    RouterError,
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
        assert result.provider_content["id"] == "chatcmpl-test"
        assert result.provider_content["choices"][0]["message"]["content"] == "Hello!"
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

    def test_invoke_maps_cache_usage_metadata_fields(self):
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class CacheUsageCapture(RequestCapture):
            def _respond(self, request):
                return Response(200, json={
                    "id": "chatcmpl-cache",
                    "model": "gpt-4o-mini",
                    "choices": [{
                        "index": 0,
                        "message": {"role": "assistant", "content": "ok"},
                        "finish_reason": "stop",
                    }],
                    "usage": {
                        "prompt_tokens": 10,
                        "completion_tokens": 2,
                        "total_tokens": 12,
                        "cache_tokens": 3,
                        "cache_read_tokens": 3,
                        "cache_miss_tokens": 7,
                        "cache_write_tokens": 1,
                        "cache_status": "observed",
                        "cache_source": "provider_usage",
                        "cache_authoritative": True,
                    },
                })

        capture = CacheUsageCapture()

        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(messages=[{"role": "user", "content": "hi"}])
            await router.close()
            return r

        result = asyncio.run(runner())
        usage = result.usage_metadata
        assert usage.cache_tokens == 3
        assert usage.cache_read_tokens == 3
        assert usage.cache_miss_tokens == 7
        assert usage.cache_write_tokens == 1
        assert usage.cache_status == "observed"
        assert usage.cache_source == "provider_usage"
        assert usage.cache_authoritative is True

    def test_invoke_treats_non_bool_cache_authoritative_as_false(self):
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class CacheUsageCapture(RequestCapture):
            def _respond(self, request):
                return Response(200, json={
                    "id": "chatcmpl-cache",
                    "choices": [{
                        "index": 0,
                        "message": {"role": "assistant", "content": "ok"},
                        "finish_reason": "stop",
                    }],
                    "usage": {
                        "prompt_tokens": 10,
                        "completion_tokens": 2,
                        "total_tokens": 12,
                        "cache_authoritative": "false",
                    },
                })

        capture = CacheUsageCapture()

        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            r = await router.invoke(messages=[{"role": "user", "content": "hi"}])
            await router.close()
            return r

        result = asyncio.run(runner())
        assert result.usage_metadata.cache_authoritative is False

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
        """PR #358 二轮意见 C：request_defaults 含保留键的校验异常不再以
        裸 ValueError 逃逸（调用方按契约捕获 RouterError 接不住，且
        REQUEST_STARTED 已发而无终态事件，在途虚高）。校验挪入 try 后
        走既有 except 分支：请求侧错误不 on_failure 不冷却、记 errors
        并继续 fallback；本例只有一个部署，无处可退 → AllDeploymentsFailed
        （RouterError 子类），errors 里带原始 ValueError 信息，raise 前
        发 EXHAUSTED 终态，事件配对闭合。
        """
        dep = Deployment(
            id="openai-1",
            model_name="gpt-4o-mini",
            api_key="sk-test",
            api_base="http://test",
            provider="openai",
            request_defaults={"model": "bad-model"},
        )
        capture = RequestCapture()
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[dep])
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            with pytest.raises(AllDeploymentsFailed) as exc_info:
                await router.invoke(messages=[{"role": "user", "content": "hi"}])
            await router.close()
            return exc_info, router

        exc_info, router = asyncio.run(runner())
        # errors 里带原始 ValueError 信息（保留键校验消息）
        errors = exc_info.value.details["errors"]
        assert len(errors) == 1
        deployment_id, error_message = errors[0]
        assert deployment_id == "openai-1"
        assert "reserved keys" in error_message
        # 校验失败发生在 HTTP 请求之前：没有任何请求发出去
        assert capture.requests == []
        # 事件配对闭合：REQUEST_STARTED + ALL_DEPLOYMENTS_EXHAUSTED
        assert [e.event_type for e in events.events] == [
            RoutingEventType.REQUEST_STARTED,
            RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
        ]
        # 请求侧错误：不 on_failure（无失败计数/冷却）
        assert router.state.consecutive_failures.get("openai-1", 0) == 0
        assert router.state.deployment_status.get("openai-1") != DeploymentStatus.COOLDOWN


    def test_completion_with_explicit_deployment(self):
        """PR #358 意见 11: ReliableRouter.completion 恢复 deployment 形参。
        显式指定部署不再 TypeError（got multiple values for keyword
        argument 'deployment'），请求直接打到指定部署（直通路径，
        不参与 state 记账）。"""
        capture = RequestCapture()
        events = EventCapture()
        dep1 = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        dep2 = Deployment(
            id="openai-2", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test2", provider="openai",
        )
        async def runner():
            router = ReliableRouter(deployments=[dep1, dep2])
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            r = await router.completion(
                "gpt-4o-mini",
                [{"role": "user", "content": "hi"}],
                deployment=dep2,
            )
            await router.close()
            return r, router
        result, router = asyncio.run(runner())

        assert isinstance(result, dict)
        # 请求打到了指定部署（而非经选路落到 openai-1）
        assert result["deployment_id"] == "openai-2"
        assert len(capture.requests) == 1
        # 直通路径保留事件配对：REQUEST_STARTED 与 REQUEST_SUCCEEDED 成对
        started = [
            e for e in events.events
            if e.event_type == RoutingEventType.REQUEST_STARTED
        ]
        succeeded = [
            e for e in events.events
            if e.event_type == RoutingEventType.REQUEST_SUCCEEDED
        ]
        assert len(started) == 1
        assert len(succeeded) == 1
        assert started[0].request_id == succeeded[0].request_id
        assert succeeded[0].deployment_id == "openai-2"
        # 直通路径不参与 state 记账
        assert router.state.total_requests == {}

    def test_completion_with_explicit_deployment_failure_pairs_events(self):
        """PR #358 意见 11: 直通路径失败时 raise 前补发 EXHAUSTED
        （failed +1），维持 successful + failed + 在途 == total_requests
        的配对不变式；error_type 保持异常类名口径。"""
        capture = RequestCapture()
        events = EventCapture()
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class ServerErrorCapture(RequestCapture):
            def _respond(self, request):
                return Response(502, text="Bad Gateway")

        capture = ServerErrorCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            try:
                await router.completion(
                    "gpt-4o-mini",
                    [{"role": "user", "content": "hi"}],
                    deployment=dep,
                )
            except Exception as e:
                await router.close()
                return e, router
            await router.close()
            return None, router
        error, router = asyncio.run(runner())

        from intelli_router.utils.exceptions import DeploymentServerError
        assert isinstance(error, DeploymentServerError)
        started = [
            e for e in events.events
            if e.event_type == RoutingEventType.REQUEST_STARTED
        ]
        exhausted = [
            e for e in events.events
            if e.event_type == RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED
        ]
        assert len(started) == 1
        assert len(exhausted) == 1
        assert started[0].request_id == exhausted[0].request_id
        assert exhausted[0].error_type == "DeploymentServerError"
        # 直通路径不参与 state 记账（无失败计数、无冷却）
        assert router.state.consecutive_failures == {}
        assert router.state.cooldown_until == {}

    def test_provider_non_dict_response_succeeds_without_failure(self):
        """PR #358 意见 6: 自定义 provider 返回非 dict 响应时，
        usage 解析按 0 处理而非 AttributeError——本地解析问题不再被
        except 当成部署失败触发冷却重试。"""
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class RawStringCapture(RequestCapture):
            def _respond(self, request):
                # 200 + 合法 JSON 字符串（非 dict）：transform_response
                # 透传后 completion 拿到的是 str
                return Response(200, text=json.dumps("raw string response"))

        capture = RawStringCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep], num_retries=3)
            attach_mock_transport(router, capture)
            r = await router.completion(
                "gpt-4o-mini", [{"role": "user", "content": "hi"}]
            )
            await router.close()
            return r, router
        result, router = asyncio.run(runner())

        # 请求成功：非 dict 响应原样返回给调用方
        assert result == "raw string response"
        # 不再被当作部署失败：没有重试（仅 1 个请求）、无失败计数、无冷却
        assert len(capture.requests) == 1
        assert router.state.consecutive_failures.get("openai-1", 0) == 0
        assert router.state.cooldown_until.get("openai-1") is None
        assert router.state.deployment_status.get(
            "openai-1", DeploymentStatus.HEALTHY
        ) != DeploymentStatus.COOLDOWN
        # 成功口径正常记录（tokens=0）
        assert router.state.total_requests.get("openai-1") == 1

    def test_completion_strips_routing_internal_keys(self):
        """PR #358 意见 9: 路由专用参数（session_id/tags）由
        RoutingContext 承载、供策略消费，不随请求发给上游 provider
        （严格网关会对未知字段返回 400）。"""
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )
        capture = RequestCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            await router.completion(
                "gpt-4o-mini",
                [{"role": "user", "content": "hi"}],
                session_id="sess-abc",
                tags=["blue", "prod"],
                temperature=0.5,
            )
            await router.close()
        asyncio.run(runner())

        body = json.loads(capture.requests[0].content)
        assert "session_id" not in body
        assert "tags" not in body
        # 业务参数正常透传
        assert body["temperature"] == 0.5

    def test_request_defaults_routing_internal_keys_stripped(self):
        """PR #358 意见 9: deployment.request_defaults 中的路由专用参数
        同样被剔除，不进 provider body。"""
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
            request_defaults={"session_id": "default-sess", "top_p": 0.9},
        )
        capture = RequestCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            await router.completion(
                "gpt-4o-mini", [{"role": "user", "content": "hi"}]
            )
            await router.close()
        asyncio.run(runner())

        body = json.loads(capture.requests[0].content)
        assert "session_id" not in body
        assert body["top_p"] == 0.9


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

    def test_stream_preserves_usage_only_chunk(self):
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class StreamCapture(RequestCapture):
            def _respond(self, request):
                content = "data: " + json.dumps({
                    "id": "chatcmpl-1",
                    "model": "gpt-4o-mini",
                    "choices": [{"index": 0, "delta": {"content": "Hello"}, "finish_reason": None}],
                }) + "\n\n"
                content += "data: " + json.dumps({
                    "id": "chatcmpl-1",
                    "model": "gpt-4o-mini",
                    "choices": [],
                    "usage": {
                        "prompt_tokens": 3,
                        "completion_tokens": 2,
                        "total_tokens": 5,
                        "completion_tokens_details": {"reasoning_tokens": 1},
                    },
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
        assert result[-1].usage_metadata.total_tokens == 5
        assert result[-1].usage_metadata.reasoning_tokens == 1
        assert result[0].provider_content["choices"][0]["delta"]["content"] == "Hello"
        assert result[-1].provider_content["usage"]["total_tokens"] == 5
        assert result[-1].response_model == "gpt-4o-mini"

    def test_stream_records_full_stream_duration_not_ttft(self):
        """PR #358 意见 8: stream() 成功后 state 记录的延迟应为整条流的
        完整耗时（time.time() - stream_start），而非 ttft——此前 ttfb 被当
        latency、tokens 恒 0，LatencyRecord.normalized 被放大成秒级，
        污染 lowest_latency/adaptive 评分。"""
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
                # 消费端放慢节奏：拉长流的总时长，但不影响 ttft
                # （首个 chunk 由 MockTransport 即时返回）
                await asyncio.sleep(0.05)
            await router.close()
            return chunks, router
        result, router = asyncio.run(runner())

        assert len(result) >= 1
        recorded = router.state.get_average_latency_raw("openai-1")
        assert recorded is not None
        # 两个 chunk × 0.05s sleep：完整耗时 >= 0.09；若误记 ttft 则 << 0.05
        assert recorded >= 0.09, f"expected full duration, got ttft: {recorded}"
        # 该流未携带 usage：tokens 记 0（不参与 normalized 统计）
        assert router.state.latencies["openai-1"][-1].tokens == 0

    def test_stream_records_tokens_from_usage_chunk(self):
        """PR #358 意见 8: 流式成功后 tokens 取流式累积的 usage（最后一份
        非空 usage，通常是 usage-only 的最后一个 chunk），而非恒 0。"""
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
                    "choices": [],
                    "usage": {"prompt_tokens": 3, "completion_tokens": 2, "total_tokens": 5},
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
            return chunks, router
        result, router = asyncio.run(runner())

        assert len(result) >= 1
        records = router.state.latencies["openai-1"]
        assert records[-1].tokens == 2
        assert router.state.total_tokens.get("openai-1") == 2

    def test_stream_without_usage_does_not_dominate_normalized_average(self):
        """PR #358 意见 8: 同一部署流式（无 usage，tokens=0）+ 非流式
        （有 usage）混合后，avg_normalized 只由 tokens>0 的非流式记录
        决定，不被流式记录主导（tokens=0 的记录不参与归一化统计，
        但 raw latency 照记）。"""
        dep = Deployment(
            id="openai-1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test", provider="openai",
        )

        class MixedCapture(RequestCapture):
            def _respond(self, request):
                body = json.loads(request.content)
                if body.get("stream"):
                    content = "data: " + json.dumps({
                        "choices": [{"index": 0, "delta": {"content": "Hello"}, "finish_reason": None}]
                    }) + "\n\n"
                    content += "data: " + json.dumps({
                        "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]
                    }) + "\n\n"
                    content += "data: [DONE]\n\n"
                    return Response(200, text=content)
                return Response(200, json={
                    "id": "chatcmpl-mixed",
                    "choices": [{"index": 0, "message": {"content": "ok"}, "finish_reason": "stop"}],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 20, "total_tokens": 30},
                })

        capture = MixedCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            # 流式请求：慢消费拉长整条流时长（无 usage → tokens=0）
            async for _ in router.stream(messages=[{"role": "user", "content": "hi"}]):
                await asyncio.sleep(0.05)
            # 非流式请求：带 usage（completion_tokens=20）
            await router.completion(
                "gpt-4o-mini", [{"role": "user", "content": "hi"}]
            )
            await router.close()
            return router
        router = asyncio.run(runner())

        records = router.state.latencies["openai-1"]
        assert len(records) == 2
        stream_record, nonstream_record = records
        assert stream_record.tokens == 0
        assert nonstream_record.tokens == 20
        # 流式记录的 raw latency（完整耗时，含消费端 sleep）
        assert stream_record.latency >= 0.09
        # avg_normalized 只由 tokens>0 的非流式记录决定
        avg_normalized = router.state.get_average_latency("openai-1")
        assert avg_normalized == pytest.approx(nonstream_record.normalized)
        # 若流式记录（normalized 退化为 latency/1 ≈ 0.1s 量级）被计入，
        # 均值会被拉高至少一个数量级
        assert avg_normalized < stream_record.latency / 2

    def test_stream_empty_response_records_full_duration(self):
        """空流（无可解析 chunk）成功时记录整条流的完整耗时（毫秒级
        正值；此前的 ttft 兜底 0.0 口径已随意见 8 废弃）。"""
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
        recorded = router.state.get_average_latency_raw("openai-1")
        assert recorded is not None
        assert 0.0 <= recorded < 1.0

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


class TestReliableRouterStreamCompletion:
    """ReliableRouter.stream_completion()（原始 chunk 路径）。"""

    def test_stream_completion_records_full_duration_and_tokens(self):
        """PR #358 意见 8: stream_completion() 成功后 latency 为整条流的
        完整耗时（而非 ttfb），tokens 取流式累积的 usage（最后一份非空
        usage，而非恒 0）。"""
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
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
                    "usage": {"prompt_tokens": 3, "completion_tokens": 2, "total_tokens": 5},
                }) + "\n\n"
                content += "data: [DONE]\n\n"
                return Response(200, text=content)

        capture = StreamCapture()
        async def runner():
            router = ReliableRouter(deployments=[dep])
            attach_mock_transport(router, capture)
            chunks = []
            async for ch in router.stream_completion(
                "gpt-4o-mini", [{"role": "user", "content": "hi"}]
            ):
                chunks.append(ch)
                await asyncio.sleep(0.05)
            await router.close()
            return chunks, router
        result, router = asyncio.run(runner())

        assert len(result) >= 1
        records = router.state.latencies["openai-1"]
        # tokens 取自最后一份非空 usage
        assert records[-1].tokens == 2
        assert router.state.total_tokens.get("openai-1") == 2
        # latency 为整条流完整耗时（两个 chunk × 0.05s 消费），而非 ttfb
        assert records[-1].latency >= 0.09


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


def _first_remaining_selector():
    async def _select(available_list, ctx):
        return available_list[0] if available_list else None
    return _select


class TestRequestDefaultsFallback:
    """PR #358 二轮意见 C：request_defaults 校验异常（ValueError）不再
    裸逃逸——挪入 try 后按请求侧错误处理，跳过配错的部署由其他部署服务。
    """

    def _deployments(self):
        d1 = Deployment(
            id="d1", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test1", provider="openai",
            # 保留键：_request_kwargs_for_deployment 会抛 ValueError
            request_defaults={"model": "bad-model"},
        )
        d2 = Deployment(
            id="d2", model_name="gpt-4o-mini",
            api_key="sk-test", api_base="http://test2", provider="openai",
        )
        return d1, d2

    def test_completion_request_defaults_bad_falls_back_to_d2(self):
        """d1 配错 → 跳过 d1（不发 HTTP）由 d2 成功服务，事件配对
        STARTED...SUCCEEDED（无 EXHAUSTED）。"""
        d1, d2 = self._deployments()
        capture = RequestCapture()
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                result = await router.completion(
                    "gpt-4o-mini", [{"role": "user", "content": "hi"}]
                )
            await router.close()
            return result

        result = asyncio.run(runner())
        # 由 d2 成功服务
        assert result["deployment_id"] == "d2"
        # d1 的校验失败发生在 HTTP 之前：唯一请求打到了 d2
        assert len(capture.requests) == 1
        assert capture.requests[0].url.host == "test2"
        # 事件配对闭合：STARTED → (RETRIED) → SUCCEEDED，无 EXHAUSTED
        types = [e.event_type for e in events.events]
        assert types[0] == RoutingEventType.REQUEST_STARTED
        assert types[-1] == RoutingEventType.REQUEST_SUCCEEDED
        assert RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED not in types
        # 请求侧错误：d1 不进失败计数/冷却（还能被后续请求选中）
        succeeded = [e for e in events.events
                     if e.event_type == RoutingEventType.REQUEST_SUCCEEDED][0]
        assert succeeded.deployment_id == "d2"

    def test_stream_completion_request_defaults_bad_falls_back_to_d2(self):
        """同上，stream_completion 路径（STREAM_STARTED...STREAM_SUCCEEDED）。"""
        d1, d2 = self._deployments()
        capture = RequestCapture()
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                chunks = [
                    chunk async for chunk in router.stream_completion(
                        "gpt-4o-mini", [{"role": "user", "content": "hi"}]
                    )
                ]
            await router.close()
            return chunks

        chunks = asyncio.run(runner())
        assert len(chunks) >= 1
        assert len(capture.requests) == 1
        assert capture.requests[0].url.host == "test2"
        types = [e.event_type for e in events.events]
        assert types[0] == RoutingEventType.STREAM_STARTED
        assert types[-1] == RoutingEventType.STREAM_SUCCEEDED
        assert RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED not in types

    def test_stream_request_defaults_bad_falls_back_to_d2(self):
        """同上，stream()（AssistantMessageChunk）路径。"""
        d1, d2 = self._deployments()
        capture = RequestCapture()
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                chunks = [
                    chunk async for chunk in router.stream(
                        messages=[{"role": "user", "content": "hi"}],
                        model="gpt-4o-mini",
                    )
                ]
            await router.close()
            return chunks

        chunks = asyncio.run(runner())
        assert len(chunks) >= 1
        assert len(capture.requests) == 1
        assert capture.requests[0].url.host == "test2"
        types = [e.event_type for e in events.events]
        assert types[0] == RoutingEventType.STREAM_STARTED
        assert types[-1] == RoutingEventType.STREAM_SUCCEEDED
        assert RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED not in types

    def test_all_deployments_bad_request_defaults_raises_all_failed(self):
        """所有部署都配错 → AllDeploymentsFailed（RouterError 子类），
        事件 STARTED + EXHAUSTED 配对。"""
        d1, d2 = self._deployments()
        d2.request_defaults = {"messages": "bad"}
        capture = RequestCapture()
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            with pytest.raises(AllDeploymentsFailed) as exc_info:
                with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                    await router.completion(
                        "gpt-4o-mini", [{"role": "user", "content": "hi"}]
                    )
            await router.close()
            return exc_info

        exc_info = asyncio.run(runner())
        errors = exc_info.value.details["errors"]
        assert len(errors) == 2
        assert all("reserved keys" in msg for _, msg in errors)
        assert capture.requests == []
        assert [e.event_type for e in events.events] == [
            RoutingEventType.REQUEST_STARTED,
            RoutingEventType.REQUEST_RETRIED,
            RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
        ]

    def test_d1_bad_defaults_d2_http_fail_no_success(self):
        """d1 配错 + d2 也失败（HTTP 500）→ EXHAUSTED 存在、无 SUCCEEDED
        （successful == 0），AllDeploymentsFailed 带两类错误。"""
        d1, d2 = self._deployments()

        class FailCapture(RequestCapture):
            def _respond(self, request):
                return Response(500, text="server error")

        capture = FailCapture()
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            router.event_bus.register(events)
            attach_mock_transport(router, capture)
            with pytest.raises(AllDeploymentsFailed) as exc_info:
                with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                    await router.completion(
                        "gpt-4o-mini", [{"role": "user", "content": "hi"}]
                    )
            await router.close()
            return exc_info, router

        exc_info, router = asyncio.run(runner())
        errors = exc_info.value.details["errors"]
        assert len(errors) == 2
        assert "reserved keys" in errors[0][1]
        assert "500" in errors[1][1]
        types = [e.event_type for e in events.events]
        assert RoutingEventType.REQUEST_STARTED in types
        assert RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED in types
        assert RoutingEventType.REQUEST_SUCCEEDED not in types
        # d1（请求侧）不进失败计数；d2（5xx 部署侧）进
        assert router.state.consecutive_failures.get("d1", 0) == 0
        assert router.state.consecutive_failures.get("d2", 0) == 1


class TestStreamRetryHeadChunkBuffering:
    """PR #358 二轮意见 B：首个可见 chunk 前的头部 chunk 进缓冲，
    流式重试切部署时消费者不收到重复的 role chunk。"""

    @staticmethod
    def _mock_streams(d1_chunk_specs, d2_chunk_specs, error=DeploymentNetworkError("d1", "interrupted")):
        """构造按 deployment.id 分流的 acompletion_stream mock。

        chunk_specs 是 (chunk_dict,) 的序列；流结束前若 error 非 None
        则在 yield 完所有 chunk 后抛出（模拟连接中断）。
        """
        async def mock_stream(model, messages, deployment, **kwargs):
            specs = d1_chunk_specs if deployment.id == "d1" else d2_chunk_specs
            for chunk in specs:
                yield chunk
            if deployment.id == "d1" and error is not None:
                raise error
        return mock_stream

    def test_stream_d1_sends_head_chunk_then_fails_consumer_gets_only_d2(self):
        """typed 路径（AssistantMessageChunk）：role-only chunk 在
        _chunk_to_assistant_message_chunk 中本就返回 None（不透传），
        typed 路径能透传的头部块是 usage-only / finish_reason-only——
        同样进缓冲：d1 发 usage 块后失败，消费者只看到 d2 的输出，
        不会先收到一个来自 d1 的孤儿 usage/finish 块。"""
        d1_usage_chunk = {
            "choices": [{"delta": {}, "finish_reason": None}],
            "usage": {"prompt_tokens": 1, "completion_tokens": 2, "total_tokens": 3},
        }
        d1_specs = [d1_usage_chunk]
        d2_specs = [
            {"choices": [{"delta": {"role": "assistant"}, "finish_reason": None}]},
            {"choices": [{"delta": {"content": "hello"}, "finish_reason": None}]},
            {"choices": [{"delta": {}, "finish_reason": "stop"}]},
        ]
        d1 = Deployment(id="d1", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t1", provider="openai")
        d2 = Deployment(id="d2", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t2", provider="openai")
        events = EventCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            router.event_bus.register(events)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                with patch.object(router, "acompletion_stream",
                                  new=self._mock_streams(d1_specs, d2_specs)):
                    chunks = [
                        ch async for ch in router.stream(
                            messages=[{"role": "user", "content": "hi"}],
                            model="gpt-4o-mini",
                        )
                    ]
            return chunks

        chunks = asyncio.run(runner())
        # 消费者只看到 d2 的输出：先内容、后带 finish_reason 的收尾块；
        # d1 的 usage 头部块被缓冲并在失败时丢弃（未 yield 给消费者）
        assert [ch.content for ch in chunks] == ["hello", ""]
        assert chunks[-1].finish_reason == "stop"
        assert not any(ch.usage_metadata is not None for ch in chunks)
        # 事件：STARTED → RETRIED → ROUTE_SELECTED → SUCCEEDED，request_id 一致
        types = [e.event_type for e in events.events]
        assert types == [
            RoutingEventType.STREAM_STARTED,
            RoutingEventType.REQUEST_RETRIED,
            RoutingEventType.STREAM_ROUTE_SELECTED,
            RoutingEventType.STREAM_SUCCEEDED,
        ]
        ids = {e.request_id for e in events.events}
        assert len(ids) == 1

    def test_stream_completion_d1_sends_role_then_fails_single_role(self):
        """同上，stream_completion（raw chunk）路径。"""
        role = {"choices": [{"delta": {"role": "assistant"}, "finish_reason": None}]}
        d1_specs = [role]
        d2_specs = [
            role,
            {"choices": [{"delta": {"content": "hello"}, "finish_reason": None}]},
            {"choices": [{"delta": {}, "finish_reason": "stop"}]},
        ]
        d1 = Deployment(id="d1", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t1", provider="openai")
        d2 = Deployment(id="d2", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t2", provider="openai")

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                with patch.object(router, "acompletion_stream",
                                  new=self._mock_streams(d1_specs, d2_specs)):
                    chunks = [
                        ch async for ch in router.stream_completion(
                            "gpt-4o-mini", [{"role": "user", "content": "hi"}]
                        )
                    ]
            return chunks

        chunks = asyncio.run(runner())
        role_chunks = [
            ch for ch in chunks
            if ch.get("choices")
            and ch["choices"][0].get("delta", {}).get("role") == "assistant"
            and not ch["choices"][0].get("delta", {}).get("content")
        ]
        assert len(role_chunks) == 1
        texts = "".join(
            ch["choices"][0].get("delta", {}).get("content", "")
            for ch in chunks if ch.get("choices")
        )
        assert "hello" in texts

    def test_stream_d1_fails_before_any_chunk_consumer_sees_only_d2(self):
        """d1 在 role chunk 之前就失败 → 消费者只看到 d2 的输出。"""
        content = {"choices": [{"delta": {"content": "world"}, "finish_reason": None}]}
        done = {"choices": [{"delta": {}, "finish_reason": "stop"}]}
        d1_specs = []  # 一个 chunk 都没发就失败
        d2_specs = [content, done]
        d1 = Deployment(id="d1", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t1", provider="openai")
        d2 = Deployment(id="d2", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t2", provider="openai")

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                with patch.object(router, "acompletion_stream",
                                  new=self._mock_streams(d1_specs, d2_specs)):
                    chunks = [
                        ch async for ch in router.stream(
                            messages=[{"role": "user", "content": "hi"}],
                            model="gpt-4o-mini",
                        )
                    ]
            return chunks

        chunks = asyncio.run(runner())
        assert [ch.content for ch in chunks] == ["world", ""]


class TestTruncatedStreamFallback:
    """PR #358 二轮意见 D（router 端集成）：上游断流（无 [DONE] 无
    finish_reason 就关连接）被判为 DeploymentNetworkError，触发 fallback。
    """

    def test_truncated_stream_falls_back_to_healthy_deployment(self):
        d1 = Deployment(id="d1", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t1", provider="openai")
        d2 = Deployment(id="d2", model_name="gpt-4o-mini",
                        api_key="sk", api_base="http://t2", provider="openai")

        class TruncatedCapture(RequestCapture):
            """d1 返回只带 role chunk 就关闭的流（无 [DONE]、无
            finish_reason）；d2 返回正常流。"""
            def _respond(self, request):
                if request.url.host == "t1":
                    return Response(200, text=(
                        "data: " + json.dumps({
                            "choices": [
                                {"index": 0,
                                 "delta": {"role": "assistant"},
                                 "finish_reason": None}
                            ]
                        }) + "\n\n"
                    ))
                content = "data: " + json.dumps({
                    "choices": [
                        {"index": 0, "delta": {"role": "assistant"},
                         "finish_reason": None}
                    ]
                }) + "\n\n"
                content += "data: " + json.dumps({
                    "choices": [
                        {"index": 0, "delta": {"content": "rescued"},
                         "finish_reason": None}
                    ]
                }) + "\n\n"
                content += "data: " + json.dumps({
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]
                }) + "\n\n"
                content += "data: [DONE]\n\n"
                return Response(200, text=content)

        capture = TruncatedCapture()

        async def runner():
            router = ReliableRouter(deployments=[d1, d2], num_retries=1)
            attach_mock_transport(router, capture)
            with patch.object(router, "_select_deployment", new=_first_remaining_selector()):
                chunks = [
                    ch async for ch in router.stream(
                        messages=[{"role": "user", "content": "hi"}],
                        model="gpt-4o-mini",
                    )
                ]
            await router.close()
            return chunks, router

        chunks, router = asyncio.run(runner())
        texts = "".join(ch.content or "" for ch in chunks)
        assert "rescued" in texts
        # 断流被计入 d1 的失败（部署侧），而不是静默成功
        assert router.state.consecutive_failures.get("d1", 0) == 1
