"""Integration tests: ReliableRouter + EventBus."""
import pytest
from unittest.mock import patch, AsyncMock
from intelli_router.router.reliable_router import ReliableRouter
from intelli_router.observability.bus import EventBus
from intelli_router.observability.events import RoutingEvent, RoutingEventType
from intelli_router.observability.handler import EventHandler
from intelli_router.utils.exceptions import RouterError, NoDeploymentAvailable


class RecordingHandler(EventHandler):
    """记录所有收到的事件"""
    def __init__(self):
        self.events: list[RoutingEvent] = []

    async def handle_event(self, event: RoutingEvent) -> None:
        self.events.append(event)

    def events_of_type(self, event_type: RoutingEventType) -> list[RoutingEvent]:
        return [e for e in self.events if e.event_type == event_type]


@pytest.fixture
def event_bus():
    return EventBus()


@pytest.fixture
def recorder(event_bus):
    handler = RecordingHandler()
    event_bus.register(handler)
    return handler


@pytest.fixture
def router_with_events(sample_deployments, event_bus):
    return ReliableRouter(
        deployments=sample_deployments,
        strategy="simple-shuffle",
        num_retries=2,
        event_bus=event_bus,
    )


# ---------- completion() tests ----------

@pytest.mark.asyncio
async def test_completion_emits_started_and_succeeded(router_with_events, recorder):
    """成功的 completion 应触发 REQUEST_STARTED + REQUEST_SUCCEEDED"""
    mock_response = {
        "choices": [{"message": {"content": "hi"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15},
    }
    with patch.object(
        router_with_events, "completion",
        wraps=router_with_events.completion
    ):
        with patch(
            "intelli_router.router.base_router.BaseRouter.completion",
            new_callable=AsyncMock,
            return_value=mock_response,
        ):
            await router_with_events.completion("gpt-4", [{"role": "user", "content": "hi"}])

    started = recorder.events_of_type(RoutingEventType.REQUEST_STARTED)
    succeeded = recorder.events_of_type(RoutingEventType.REQUEST_SUCCEEDED)

    assert len(started) == 1
    assert len(succeeded) == 1
    assert started[0].model == "gpt-4"
    assert succeeded[0].model == "gpt-4"
    assert succeeded[0].latency is not None
    assert succeeded[0].latency >= 0
    assert succeeded[0].prompt_tokens == 10
    assert succeeded[0].completion_tokens == 5
    assert succeeded[0].total_tokens == 15
    assert succeeded[0].deployment_id is not None

    # 同一请求共享 request_id
    assert started[0].request_id == succeeded[0].request_id


@pytest.mark.asyncio
async def test_completion_emits_retried_on_failure(router_with_events, recorder):
    """第一次失败后重试，应触发 REQUEST_RETRIED"""
    mock_response = {
        "choices": [{"message": {"content": "ok"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 5, "completion_tokens": 2, "total_tokens": 7},
    }
    call_count = {"n": 0}

    async def side_effect(*args, **kwargs):
        call_count["n"] += 1
        if call_count["n"] == 1:
            raise RuntimeError("connection failed")
        return mock_response

    with patch(
        "intelli_router.router.base_router.BaseRouter.completion",
        new_callable=AsyncMock,
        side_effect=side_effect,
    ):
        await router_with_events.completion("gpt-4", [{"role": "user", "content": "hi"}])

    retried = recorder.events_of_type(RoutingEventType.REQUEST_RETRIED)
    succeeded = recorder.events_of_type(RoutingEventType.REQUEST_SUCCEEDED)

    assert len(retried) == 1
    assert retried[0].error_type == "RuntimeError"
    assert retried[0].error_message == "connection failed"
    assert retried[0].attempt == 1
    assert len(succeeded) == 1


@pytest.mark.asyncio
async def test_completion_emits_exhausted_when_all_fail(router_with_events, recorder):
    """所有尝试失败后触发 ALL_DEPLOYMENTS_EXHAUSTED"""
    with patch(
        "intelli_router.router.base_router.BaseRouter.completion",
        new_callable=AsyncMock,
        side_effect=RuntimeError("fail"),
    ):
        with pytest.raises(RouterError):
            await router_with_events.completion("gpt-4", [{"role": "user", "content": "hi"}])

    exhausted = recorder.events_of_type(RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED)
    assert len(exhausted) == 1
    assert exhausted[0].latency is not None


@pytest.mark.asyncio
async def test_completion_no_deployment_emits_exhausted(event_bus, recorder):
    """无可用 deployment 时触发 ALL_DEPLOYMENTS_EXHAUSTED"""
    from intelli_router.core.deployment import Deployment
    router = ReliableRouter(
        deployments=[Deployment(id="dep-1", model_name="claude-3", provider="anthropic", api_key="k", api_base="https://api.anthropic.com")],
        event_bus=event_bus,
    )
    with pytest.raises(NoDeploymentAvailable):
        await router.completion("nonexistent-model", [{"role": "user", "content": "hi"}])

    exhausted = recorder.events_of_type(RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED)
    assert len(exhausted) == 1
    assert exhausted[0].error_message == "No available deployments"


@pytest.mark.asyncio
async def test_request_id_consistent_across_events(router_with_events, recorder):
    """同一请求的所有事件共享相同 request_id"""
    call_count = {"n": 0}
    mock_response = {
        "choices": [{"message": {"content": "ok"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 5, "completion_tokens": 2, "total_tokens": 7},
    }

    async def side_effect(*args, **kwargs):
        call_count["n"] += 1
        if call_count["n"] == 1:
            raise RuntimeError("fail")
        return mock_response

    with patch(
        "intelli_router.router.base_router.BaseRouter.completion",
        new_callable=AsyncMock,
        side_effect=side_effect,
    ):
        await router_with_events.completion("gpt-4", [{"role": "user", "content": "hi"}])

    request_ids = {e.request_id for e in recorder.events}
    assert len(request_ids) == 1
    rid = request_ids.pop()
    assert len(rid) == 12
    assert all(c in "0123456789abcdef" for c in rid)


# ---------- stream() tests ----------

@pytest.mark.asyncio
async def test_stream_emits_started_and_succeeded(router_with_events, recorder):
    """成功的 stream 应触发 STREAM_STARTED + STREAM_SUCCEEDED"""
    chunks = [
        {"choices": [{"delta": {"content": "hello"}, "finish_reason": None}]},
        {"choices": [{"delta": {"content": " world"}, "finish_reason": None}]},
        {"choices": [{"delta": {}, "finish_reason": "stop"}]},
    ]

    async def mock_stream(*args, **kwargs):
        for chunk in chunks:
            yield chunk

    with patch.object(
        router_with_events, "acompletion_stream",
        side_effect=mock_stream,
    ):
        collected = []
        async for chunk in router_with_events.stream(
            [{"role": "user", "content": "hi"}], model="gpt-4"
        ):
            collected.append(chunk)

    started = recorder.events_of_type(RoutingEventType.STREAM_STARTED)
    succeeded = recorder.events_of_type(RoutingEventType.STREAM_SUCCEEDED)

    assert len(started) == 1
    assert len(succeeded) == 1
    assert succeeded[0].chunk_count == len(collected)
    assert succeeded[0].latency is not None
    assert started[0].request_id == succeeded[0].request_id


@pytest.mark.asyncio
async def test_stream_emits_retried_and_exhausted(router_with_events, recorder):
    """stream 所有尝试失败后触发 REQUEST_RETRIED + ALL_DEPLOYMENTS_EXHAUSTED"""
    with patch.object(
        router_with_events, "acompletion_stream",
        side_effect=RuntimeError("stream failed"),
    ):
        with pytest.raises(RouterError):
            async for _ in router_with_events.stream(
                [{"role": "user", "content": "hi"}], model="gpt-4"
            ):
                pass

    retried = recorder.events_of_type(RoutingEventType.REQUEST_RETRIED)
    exhausted = recorder.events_of_type(RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED)

    # num_retries=2, 所以最多 3 次尝试，2 次 retry 事件
    assert len(retried) >= 1
    assert len(exhausted) == 1


# ---------- backward compatibility ----------

@pytest.mark.asyncio
async def test_no_event_bus_param_works(sample_deployments):
    """不传 event_bus 参数时路由正常工作"""
    router = ReliableRouter(
        deployments=sample_deployments,
        strategy="simple-shuffle",
        num_retries=1,
    )
    mock_response = {
        "choices": [{"message": {"content": "ok"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 5, "completion_tokens": 2, "total_tokens": 7},
    }
    with patch(
        "intelli_router.router.base_router.BaseRouter.completion",
        new_callable=AsyncMock,
        return_value=mock_response,
    ):
        result = await router.completion("gpt-4", [{"role": "user", "content": "hi"}])
    assert result["choices"][0]["message"]["content"] == "ok"
