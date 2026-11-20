"""
IntelliRouter - 公开接口
"""
# 运行期版本号：优先取安装元数据（发行包名 "intelli-router"），
# 未安装（如源码直接引用）时回退到 pyproject 声明的版本。
try:
    from importlib.metadata import PackageNotFoundError, version as _pkg_version
    try:
        __version__ = _pkg_version("intelli-router")
    except PackageNotFoundError:
        __version__ = "0.1.0"
except ImportError:  # pragma: no cover - Python <3.8 兜底（本项目要求 >=3.9，防御性保留）
    __version__ = "0.1.0"

from .router.reliable_router import ReliableRouter, MODEL_WILDCARD
from .router.base_router import BaseRouter
from .core.deployment import Deployment, DeploymentStatus
from .strategy.base_strategy import RoutingStrategy
from .strategy import StrategyType
from .core.state import TokenUsage
from .provider.base_provider import BaseProviderAdapter
from .types import (
    ToolCall,
    UsageMetadata,
    AssistantMessage,
    AssistantMessageChunk,
    ImageGenerationResponse,
    AudioGenerationResponse,
    VideoGenerationResponse,
)

from .parser import (
    BaseOutputParser,
    JsonOutputParser,
    MarkdownOutputParser,
)

from .observability import (
    EventBus,
    EventHandler,
    RoutingEvent,
    RoutingEventType,
    LoggingHook,
    MetricsCollector,
    MetricsWebServer,
)

__all__ = [
    "__version__",
    "ReliableRouter",
    "MODEL_WILDCARD",
    "BaseRouter",
    "Deployment",
    "DeploymentStatus",
    "RoutingStrategy",
    "StrategyType",
    "TokenUsage",
    "BaseProviderAdapter",
    "ToolCall",
    "UsageMetadata",
    "AssistantMessage",
    "AssistantMessageChunk",
    "ImageGenerationResponse",
    "AudioGenerationResponse",
    "VideoGenerationResponse",
    "BaseOutputParser",
    "JsonOutputParser",
    "MarkdownOutputParser",
    # Observability
    "EventBus",
    "EventHandler",
    "RoutingEvent",
    "RoutingEventType",
    "LoggingHook",
    "MetricsCollector",
    "MetricsWebServer",
]
