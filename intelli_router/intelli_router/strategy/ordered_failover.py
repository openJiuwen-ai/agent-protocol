"""Ordered failover strategy."""
from typing import List, Optional, TYPE_CHECKING

from .base_strategy import RoutingStrategy

if TYPE_CHECKING:
    from ..core.context import RoutingContext
    from ..core.deployment import Deployment


class OrderedFailoverStrategy(RoutingStrategy):
    """Always choose the first available deployment in the current order."""
    strict_fallback_errors = True

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext",
    ) -> Optional["Deployment"]:
        import time

        now = time.time()
        for deployment in deployments:
            if deployment.is_available(now):
                return deployment
        return None

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        return None

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        return None
