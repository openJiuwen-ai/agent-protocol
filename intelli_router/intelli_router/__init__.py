"""
IntelliRouter - 公开接口
"""
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
