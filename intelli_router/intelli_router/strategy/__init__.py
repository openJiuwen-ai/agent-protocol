"""
SDK LLM Strategy Module

"""
from typing import Union, Literal, Optional
from .base_strategy import RoutingStrategy
from .simple_shuffle import SimpleShuffleStrategy
from .lowest_latency import LowestLatencyStrategy
from .tag_based import TagBasedStrategy
from .token_aware import TokenAwareStrategy
from .rate_limit_aware import RateLimitAwareStrategy
from .adaptive import AdaptiveStrategy

from ..core.state import LocalRouterState

StrategyType = Literal["simple-shuffle", "lowest-latency", "tag-based", "token-aware", "rate-limit-aware", "adaptive"]

__all__ = [
    "RoutingStrategy", "SimpleShuffleStrategy", "LowestLatencyStrategy", "TagBasedStrategy",
    "TokenAwareStrategy", "RateLimitAwareStrategy", "AdaptiveStrategy",
    "StrategyType", "create_strategy"
]

def create_strategy(
    strategy_type: Union[StrategyType, str],
    state: Optional[LocalRouterState] = None,
    **kwargs
) -> RoutingStrategy:
    """创建策略实例"""
    if strategy_type == "simple-shuffle":
        return SimpleShuffleStrategy(**kwargs)
    elif strategy_type == "lowest-latency":
        if state is None:
            raise ValueError("lowest-latency strategy requires state")
        return LowestLatencyStrategy(state=state, **kwargs)
    elif strategy_type == "tag-based":
        return TagBasedStrategy(**kwargs)
    elif strategy_type == "token-aware":
        if state is None:
            raise ValueError("token-aware strategy requires state")
        return TokenAwareStrategy(state=state, **kwargs)
    elif strategy_type == "rate-limit-aware":
        if state is None:
            raise ValueError("rate-limit-aware strategy requires state")
        return RateLimitAwareStrategy(state=state, **kwargs)
    elif strategy_type == "adaptive":
        if state is None:
            raise ValueError("adaptive strategy requires state")
        return AdaptiveStrategy(state=state, **kwargs)
    else:
        raise ValueError(f"Unknown strategy: {strategy_type}")
