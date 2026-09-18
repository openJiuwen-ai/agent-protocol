"""结构化日志事件处理器"""
import json
import logging
from typing import Optional

from .events import RoutingEvent
from .handler import EventHandler


class LoggingHook(EventHandler):
    """标准日志 Hook

    将路由事件输出到 Python logging 系统。
    支持 JSON 和人类可读文本两种格式。

    用法:
        hook = LoggingHook()                           # JSON 格式，INFO 级别
        hook = LoggingHook(format="text")              # 人类可读文本
        hook = LoggingHook(logger_name="my.logger")    # 自定义 logger
    """

    def __init__(
        self,
        logger_name: str = "intelli_router",
        level: int = logging.INFO,
        format: str = "json",
    ):
        """
        Args:
            logger_name: Python logger 名称
            level: 日志级别
            format: 输出格式 - "json" 或 "text"
        """
        self._logger = logging.getLogger(logger_name)
        self._level = level
        self._format = format

    async def handle_event(self, event: RoutingEvent) -> None:
        """将事件格式化并输出到 logger"""
        message = self._format_event(event)
        self._logger.log(self._level, message)

    def _format_event(self, event: RoutingEvent) -> str:
        if self._format == "json":
            return self._format_json(event)
        return self._format_text(event)

    def _format_json(self, event: RoutingEvent) -> str:
        return json.dumps(event.to_dict(), ensure_ascii=False)

    def _format_text(self, event: RoutingEvent) -> str:
        parts = [
            f"[{event.event_type.value}]",
            f"req={event.request_id[:8]}",
            f"model={event.model}",
        ]
        if event.deployment_id:
            parts.append(f"dep={event.deployment_id}")
        if event.provider:
            parts.append(f"provider={event.provider}")
        if event.latency is not None:
            parts.append(f"lat={event.latency:.3f}s")
        if event.total_tokens is not None:
            parts.append(f"tokens={event.total_tokens}")
        if event.attempt > 0:
            parts.append(f"attempt={event.attempt}/{event.total_attempts}")
        if event.error_message:
            parts.append(f"err={event.error_message}")
        if event.chunk_count is not None:
            parts.append(f"chunks={event.chunk_count}")
        return " ".join(parts)
