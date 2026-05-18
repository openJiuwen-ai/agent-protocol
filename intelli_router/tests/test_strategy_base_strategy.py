"""Tests for intelli_router.strategy.base_strategy."""
import pytest
from intelli_router.strategy.base_strategy import RoutingStrategy


def test_routing_strategy_is_abstract():
    """RoutingStrategy cannot be instantiated directly."""
    with pytest.raises(TypeError):
        RoutingStrategy()


def test_concrete_subclass_works():
    """A minimal concrete subclass can be instantiated."""
    class MinimalStrategy(RoutingStrategy):
        async def select_deployment(self, deployments, context):
            return None

        def on_success(self, deployment, latency, tokens):
            pass

        def on_failure(self, deployment, error):
            pass

    strategy = MinimalStrategy()
    assert isinstance(strategy, RoutingStrategy)
