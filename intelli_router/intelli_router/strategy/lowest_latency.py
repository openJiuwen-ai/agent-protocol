"""
SDK LLM Strategy - 最低延迟

LowestLatencyStrategy: 选择平均延迟最低的部署
"""
from typing import List, Optional, TYPE_CHECKING
import time
from .base_strategy import RoutingStrategy
if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState

class LowestLatencyStrategy(RoutingStrategy):
    """
    最低延迟策略 - 选择平均延迟最低的部署

    答法:
    1. 过滤可用部署
    2. 获每个部署的平均归一化延迟
    3. 择延迟最低的
    """

    def __init__(
        self,
        state: "LocalRouterState",
        exploration_ratio: float = 0.1
    ):
        self.state = state
        self.exploration_ratio = exploration_ratio

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """选择延迟最低的部署"""
        now = time.time()
        # 过滤可用部署
        available = [d for d in deployments if d.is_available(now)]
        if not available:
            return None
        # 探索: 箄机选择一个
        import random
        if random.random() < self.exploration_ratio:
            return random.choice(available)
        # 利用: 择延迟最低的
        best_deployment = None
        best_latency = float('inf')
        for d in available:
            avg_latency = self.state.get_average_latency(d.id)
            if avg_latency < best_latency:
                best_latency = avg_latency
                best_deployment = d
        return best_deployment or available[0]

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        """成功回调 - 更新延迟记录"""
        self.state.on_success(deployment.id, latency, tokens)

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        """失败回调 - 更新失败记录"""
        self.state.on_failure(deployment.id, error)
