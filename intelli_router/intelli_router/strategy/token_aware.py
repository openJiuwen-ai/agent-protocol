"""
SDK LLM Strategy - Token感知

TokenAwareStrategy: Token耗尽无缝切换策略
"""
from typing import List, Optional, TYPE_CHECKING
import random
from .base_strategy import RoutingStrategy

if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState


class TokenAwareStrategy(RoutingStrategy):
    """
    Token感知策略 - Token耗尽时无缝切换

    传入的 deployments 已由 router 按可用性过滤（state 唯一事实源）。

    策略:
    1. 按Token剩余量排序
    2. 优先选择Token充足的部署
    3. 无Token时无缝切换到下一个
    """

    def __init__(
        self,
        state: "LocalRouterState",
        token_threshold: int = 1000,
        exploration_ratio: float = 0.1
    ):
        self.state = state
        self.token_threshold = token_threshold
        self.exploration_ratio = exploration_ratio

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """选择Token充足的部署"""
        if not deployments:
            return None

        # 探索: 随机选择一个
        if random.random() < self.exploration_ratio:
            return random.choice(deployments)

        # 利用: 按Token剩余量排序
        scored = []
        for d in deployments:
            token_remaining = self.state.get_token_remaining(d.id)
            scored.append((d, token_remaining))

        # 降序排序 (剩余多的优先)
        scored.sort(key=lambda x: x[1], reverse=True)

        # 选择Token充足的部署
        for d, remaining in scored:
            if remaining >= self.token_threshold or remaining == float('inf'):
                return d

        # 都不充足，选剩余最多的
        return scored[0][0] if scored else deployments[0]

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        """成功回调 - 无额外策略状态（router 层已统一更新 state）"""
        pass

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        """失败回调 - 无额外策略状态（router 层已统一更新 state）"""
        pass
