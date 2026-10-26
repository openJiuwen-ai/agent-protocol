"""Tests for intelli_router.observability.bus."""
import pytest
from intelli_router.observability.bus import EventBus
from intelli_router.observability.events import RoutingEvent, RoutingEventType
from intelli_router.observability.handler import EventHandler


class RecordingHandler(EventHandler):
    """测试用 handler，记录收到的事件"""
    def __init__(self):
        self.events = []

    async def handle_event(self, event: RoutingEvent) -> None:
        self.events.append(event)


class FailingHandler(EventHandler):
    """测试用 handler，总是抛异常"""
    async def handle_event(self, event: RoutingEvent) -> None:
        raise RuntimeError("handler error")


def _make_event() -> RoutingEvent:
    return RoutingEvent(
        event_type=RoutingEventType.REQUEST_STARTED,
        request_id="test123",
        model="gpt-4",
    )


@pytest.mark.asyncio
class TestEventBus:
    async def test_empty_bus_emit_no_error(self):
        bus = EventBus()
        await bus.emit(_make_event())  # 不应抛异常

    async def test_register_and_emit(self):
        bus = EventBus()
        handler = RecordingHandler()
        bus.register(handler)

        event = _make_event()
        await bus.emit(event)

        assert len(handler.events) == 1
        assert handler.events[0] is event

    async def test_multiple_handlers_all_called(self):
        bus = EventBus()
        h1 = RecordingHandler()
        h2 = RecordingHandler()
        bus.register(h1)
        bus.register(h2)

        await bus.emit(_make_event())

        assert len(h1.events) == 1
        assert len(h2.events) == 1

    async def test_handler_exception_suppressed(self):
        bus = EventBus()
        bus.register(FailingHandler())

        # 不应抛异常到调用方
        await bus.emit(_make_event())

    async def test_handler_exception_does_not_block_others(self):
        bus = EventBus()
        failing = FailingHandler()
        recording = RecordingHandler()
        bus.register(failing)
        bus.register(recording)

        await bus.emit(_make_event())

        # 即使前一个 handler 失败，后一个仍然收到事件
        assert len(recording.events) == 1

    async def test_unregister(self):
        bus = EventBus()
        handler = RecordingHandler()
        bus.register(handler)
        bus.unregister(handler)

        await bus.emit(_make_event())

        assert len(handler.events) == 0

    async def test_handlers_property_returns_copy(self):
        bus = EventBus()
        handler = RecordingHandler()
        bus.register(handler)

        handlers = bus.handlers
        handlers.clear()  # 修改副本

        # 原始列表不受影响
        assert len(bus.handlers) == 1

    async def test_multiple_emits(self):
        bus = EventBus()
        handler = RecordingHandler()
        bus.register(handler)

        await bus.emit(_make_event())
        await bus.emit(_make_event())
        await bus.emit(_make_event())

        assert len(handler.events) == 3
