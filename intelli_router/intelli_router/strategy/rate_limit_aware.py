"""
SDK LLM Strategy - RPM限流感知

RateLimitAwareStrategy: RPM限流负载均衡策略
"""
from typing import List, Optional, TYPE_CHECKING
import random
from .base_strategy import RoutingStrategy

if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState


class RateLimitAwareStrategy(RoutingStrategy):
    """
    RPM限流感知策略 - RPM限流时负载均衡

    传入的 deployments 已由 router 按可用性过滤（state 唯一事实源）。

    策略:
    1. 按RPM剩余配额排序
    2. 优先选择RPM配额充足的部署
    3. 无RPM配额时无缝切换
    """

    def __init__(
        self,
        state: "LocalRouterState",
        rpm_threshold: int = 10,
        exploration_ratio: float = 0.1
    ):
        self.state = state
        self.rpm_threshold = rpm_threshold
        self.exploration_ratio = exploration_ratio

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """选择RPM配额充足的部署"""
        if not deployments:
            return None

        # 探索: 随机选择一个
        if random.random() < self.exploration_ratio:
            return random.choice(deployments)

        # 利用: 按RPM剩余量排序
        scored = []
        for d in deployments:
            rpm_remaining = self.state.get_rpm_remaining(d.id)
            scored.append((d, rpm_remaining))

        # 降序排序 (剩余多的优先)
        scored.sort(key=lambda x: x[1], reverse=True)

        # 选择RPM充足的部署
        for d, remaining in scored:
            if remaining >= self.rpm_threshold or remaining == float('inf'):
                return d

        # 都不充足，选剩余最多的
        return scored[0][0] if scored else deployments[0]

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        """成功回调 - 无额外策略状态（router 层已统一更新 state）"""
        pass

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        """失败回调 - 无额外策略状态（router 层已统一更新 state）"""
        pass
