"""路由事件定义"""
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, Dict, Any
import time
import uuid


class RoutingEventType(str, Enum):
    """路由事件类型"""
    # 请求生命周期
    REQUEST_STARTED = "request_started"
    REQUEST_SUCCEEDED = "request_succeeded"
    REQUEST_RETRIED = "request_retried"
    ALL_DEPLOYMENTS_EXHAUSTED = "all_deployments_exhausted"

    # 流式请求
    STREAM_STARTED = "stream_started"
    STREAM_SUCCEEDED = "stream_succeeded"


@dataclass
class RoutingEvent:
    """一次路由事件的完整信息

    不携带请求 body (messages) 以避免敏感数据泄露和内存开销。
    """
    # 必填字段
    event_type: RoutingEventType
    request_id: str
    model: str

    # 时间戳
    timestamp: float = field(default_factory=time.time)

    # 部署信息
    deployment_id: Optional[str] = None
    provider: Optional[str] = None

    # 性能数据
    latency: Optional[float] = None
    prompt_tokens: Optional[int] = None
    completion_tokens: Optional[int] = None
    total_tokens: Optional[int] = None

    # 重试信息
    attempt: int = 0
    total_attempts: int = 0

    # 错误信息
    error_type: Optional[str] = None
    error_message: Optional[str] = None

    # 流式特有
    chunk_count: Optional[int] = None

    # 扩展字段
    extra: Dict[str, Any] = field(default_factory=dict)

    @staticmethod
    def new_request_id() -> str:
        """生成短唯一请求 ID"""
        return uuid.uuid4().hex[:12]

    def to_dict(self) -> Dict[str, Any]:
        """序列化为字典，省略 None 字段"""
        result: Dict[str, Any] = {
            "event_type": self.event_type.value,
            "request_id": self.request_id,
            "model": self.model,
            "timestamp": self.timestamp,
        }
        optional_fields = [
            "deployment_id", "provider", "latency",
            "prompt_tokens", "completion_tokens", "total_tokens",
            "error_type", "error_message", "chunk_count",
        ]
        for attr in optional_fields:
            val = getattr(self, attr)
            if val is not None:
                result[attr] = val
        if self.attempt > 0:
            result["attempt"] = self.attempt
        if self.total_attempts > 0:
            result["total_attempts"] = self.total_attempts
        if self.extra:
            result["extra"] = self.extra
        return result
