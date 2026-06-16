"""事件总线 - 管理 EventHandler 注册和事件分发"""
import logging
from typing import List

from .events import RoutingEvent
from .handler import EventHandler

logger = logging.getLogger(__name__)


class EventBus:
    """事件总线

    管理 EventHandler 的注册和事件分发。
    无 handler 注册时 emit() 几乎零开销。
    事件通过直接 await 分发，单个 handler 异常不影响其他 handler 和路由主路径。
    """

    def __init__(self):
        self._handlers: List[EventHandler] = []

    @property
    def handlers(self) -> List[EventHandler]:
        """已注册的 handler 列表（只读副本）"""
        return list(self._handlers)

    def register(self, handler: EventHandler) -> None:
        """注册一个事件处理器"""
        self._handlers.append(handler)

    def unregister(self, handler: EventHandler) -> None:
        """注销一个事件处理器"""
        self._handlers.remove(handler)

    async def emit(self, event: RoutingEvent) -> None:
        """分发事件到所有已注册的处理器

        每个 handler 依次 await。如果某个 handler 抛异常，
        异常被捕获并记录日志，不会影响后续 handler 或请求路径。
        """
        if not self._handlers:
            return
        for handler in self._handlers:
            try:
                await handler.handle_event(event)
            except Exception:
                logger.debug(
                    "EventHandler %s failed for event %s",
                    handler.name, event.event_type.value,
                    exc_info=True,
                )
