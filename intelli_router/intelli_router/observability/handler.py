"""事件处理器抽象基类"""
from abc import ABC, abstractmethod

from .events import RoutingEvent


class EventHandler(ABC):
    """事件处理器基类

    用户/集成方实现此接口来处理路由事件。
    实现应避免抛出异常，内部处理错误。
    """

    @abstractmethod
    async def handle_event(self, event: RoutingEvent) -> None:
        """处理一个路由事件"""
        ...

    @property
    def name(self) -> str:
        """处理器名称（用于日志标识）"""
        return self.__class__.__name__
