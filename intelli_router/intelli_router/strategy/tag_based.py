"""
SDK LLM Strategy - 标签路由

TagBasedStrategy: 指签过滤部署
"""
from typing import List, Optional, Set, TYPE_CHECKING
from .base_strategy import RoutingStrategy
from .simple_shuffle import SimpleShuffleStrategy
if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState

class TagBasedStrategy(RoutingStrategy):
    """
    标签路由策略 - 按标签过滤部署

    答法:
    1. 从上下文提取求签
    2. 过滤匹配标签的部署
    3. 从匹配中选择 (可组合其他策略)
    """

    def __init__(
        self,
        fallback_strategy: Optional[RoutingStrategy] = None
    ):
        self.fallback_strategy = fallback_strategy or SimpleShuffleStrategy()

    def _get_request_tags(self, context: "RoutingContext") -> Set[str]:
        """从上下文提取请求标签"""
        # 1. 显式标签
        if context.request_tags:
            return set(context.request_tags)
        # 2. 从kwargs提取
        tags = context.kwargs.get("tags", [])
        if tags:
            return set(tags)
        # 3. 从消息推断 (简化: 使用模型名)
        return {context.model}

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """按标签过滤后选择"""
        import time
        now = time.time()
        # 过滤可用部署
        available = [d for d in deployments if d.is_available(now)]
        if not available:
            return None
        # 获取请求标签
        request_tags = self._get_request_tags(context)
        # 过滤匹配标签的部署
        matched = []
        for d in available:
            if d.tags and request_tags & set(d.tags):
                matched.append(d)
        # 无匹配则使用所有可用
        if not matched:
            matched = available
        # 委托给fallback策略
        return await self.fallback_strategy.select_deployment(matched, context)

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        """成功回调"""
        self.fallback_strategy.on_success(deployment, latency, tokens)

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        """失败回调"""
        self.fallback_strategy.on_failure(deployment, error)
