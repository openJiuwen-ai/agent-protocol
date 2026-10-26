"""Ordered failover strategy."""
from typing import List, Optional, TYPE_CHECKING

from .base_strategy import RoutingStrategy

if TYPE_CHECKING:
    from ..core.context import RoutingContext
    from ..core.deployment import Deployment


class OrderedFailoverStrategy(RoutingStrategy):
    """Always choose the first deployment in the given (already availability-
    filtered) order. Availability filtering is owned by the router; state is
    the single source of truth.
    """
    strict_fallback_errors = True

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext",
    ) -> Optional["Deployment"]:
        return deployments[0] if deployments else None

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        return None

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        return None
