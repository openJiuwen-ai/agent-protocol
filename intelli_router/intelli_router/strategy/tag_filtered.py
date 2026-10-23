"""Strict fallback-tag filtered routing strategy."""
from typing import List, Optional, TYPE_CHECKING

from .base_strategy import RoutingStrategy
from .ordered_failover import OrderedFailoverStrategy

if TYPE_CHECKING:
    from ..core.context import RoutingContext
    from ..core.deployment import Deployment


class TagFilteredStrategy(RoutingStrategy):
    """Filter deployments by fallback_tag, then delegate selection.

    Deployments passed in are already availability-filtered by the router
    (state is the single source of truth).

    ``model_description`` is intentionally not used for routing. It is carried
    on Deployment for future cluster-level model understanding.
    """
    strict_fallback_errors = True
    fallback_to_first_on_empty_selection = False

    def __init__(
        self,
        fallback_tag: Optional[str] = None,
        fallback_strategy: Optional[RoutingStrategy] = None,
    ):
        self.fallback_tag = fallback_tag
        self.fallback_strategy = fallback_strategy or OrderedFailoverStrategy()

    def _request_fallback_tag(self) -> Optional[str]:
        return str(self.fallback_tag) if self.fallback_tag else None

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext",
    ) -> Optional["Deployment"]:
        if not deployments:
            return None

        fallback_tag = self._request_fallback_tag()
        if fallback_tag:
            deployments = [
                deployment
                for deployment in deployments
                if deployment.fallback_tag == fallback_tag
            ]
        if not deployments:
            return None
        return await self.fallback_strategy.select_deployment(deployments, context)

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        self.fallback_strategy.on_success(deployment, latency, tokens)

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        self.fallback_strategy.on_failure(deployment, error)
