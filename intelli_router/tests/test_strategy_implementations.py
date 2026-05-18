"""Tests for all strategy implementations and the create_strategy factory."""
import time
import random
import pytest
from unittest.mock import AsyncMock, MagicMock
from intelli_router.strategy import (
    create_strategy, SimpleShuffleStrategy, LowestLatencyStrategy,
    TagBasedStrategy, TokenAwareStrategy, RateLimitAwareStrategy,
    AdaptiveStrategy,
)
from intelli_router.core.deployment import Deployment, DeploymentStatus
from intelli_router.core.context import RoutingContext
from intelli_router.core.state import LocalRouterState
from intelli_router.core.state import TokenUsage


# ======== create_strategy factory ========

def test_create_simple_shuffle():
    s = create_strategy("simple-shuffle")
    assert isinstance(s, SimpleShuffleStrategy)


def test_create_lowest_latency(router_state):
    s = create_strategy("lowest-latency", state=router_state)
    assert isinstance(s, LowestLatencyStrategy)


def test_create_lowest_latency_no_state():
    with pytest.raises(ValueError, match="requires state"):
        create_strategy("lowest-latency")


def test_create_tag_based():
    s = create_strategy("tag-based")
    assert isinstance(s, TagBasedStrategy)


def test_create_token_aware(router_state):
    s = create_strategy("token-aware", state=router_state)
    assert isinstance(s, TokenAwareStrategy)


def test_create_token_aware_no_state():
    with pytest.raises(ValueError, match="requires state"):
        create_strategy("token-aware")


def test_create_rate_limit_aware(router_state):
    s = create_strategy("rate-limit-aware", state=router_state)
    assert isinstance(s, RateLimitAwareStrategy)


def test_create_rate_limit_aware_no_state():
    with pytest.raises(ValueError, match="requires state"):
        create_strategy("rate-limit-aware")


def test_create_adaptive(router_state):
    s = create_strategy("adaptive", state=router_state)
    assert isinstance(s, AdaptiveStrategy)


def test_create_adaptive_no_state():
    with pytest.raises(ValueError, match="requires state"):
        create_strategy("adaptive")


def test_create_unknown():
    with pytest.raises(ValueError, match="Unknown strategy"):
        create_strategy("nonexistent")


def test_create_passes_kwargs(router_state):
    s = create_strategy("adaptive", state=router_state, w_health=2.0, w_token=1.5)
    assert s.w_health == 2.0
    assert s.w_token == 1.5


# ======== SimpleShuffleStrategy ========

@pytest.mark.asyncio
async def test_simple_shuffle_select(deployment_gpt4_1, deployment_gpt4_2):
    strategy = SimpleShuffleStrategy()
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_simple_shuffle_empty():
    strategy = SimpleShuffleStrategy()
    ctx = RoutingContext(model="m", messages=[])
    assert await strategy.select_deployment([], ctx) is None


@pytest.mark.asyncio
async def test_simple_shuffle_weights(deployment_gpt4_1, deployment_gpt4_2):
    """With highly skewed weights, the heavy deployment should be selected more often."""
    random.seed(42)
    strategy = SimpleShuffleStrategy(
        weights={"dep_gpt4_1": 100, "dep_gpt4_2": 1},
    )
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selections = {"dep_gpt4_1": 0, "dep_gpt4_2": 0}
    for _ in range(100):
        s = await strategy.select_deployment(deps, ctx)
        selections[s.id] += 1
    assert selections["dep_gpt4_1"] > selections["dep_gpt4_2"]


@pytest.mark.asyncio
async def test_simple_shuffle_custom_default_weight(deployment_gpt4_1, deployment_gpt4_2):
    strategy = SimpleShuffleStrategy(default_weight=0.5)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected is not None


def test_simple_shuffle_on_success_on_failure_noop(deployment_gpt4_1):
    strategy = SimpleShuffleStrategy()
    strategy.on_success(deployment_gpt4_1, latency=0.1, tokens=10)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))


# ======== LowestLatencyStrategy ========

@pytest.mark.asyncio
async def test_lowest_latency_select(populated_state, deployment_gpt4_1, deployment_gpt4_2):
    strategy = LowestLatencyStrategy(state=populated_state, exploration_ratio=0.0)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    # dep_gpt4_1 has lower avg latency (0.004) than dep_gpt4_2 (0.006)
    assert selected.id == "dep_gpt4_1"


@pytest.mark.asyncio
async def test_lowest_latency_empty_state(deployment_gpt4_1, deployment_gpt4_2):
    """No latency records -> all inf, picks first available."""
    state = LocalRouterState()
    strategy = LowestLatencyStrategy(state=state, exploration_ratio=0.0)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected is not None


@pytest.mark.asyncio
async def test_lowest_latency_empty_deps():
    state = LocalRouterState()
    strategy = LowestLatencyStrategy(state=state)
    ctx = RoutingContext(model="m", messages=[])
    assert await strategy.select_deployment([], ctx) is None


@pytest.mark.asyncio
async def test_lowest_latency_exploration(deployment_gpt4_1, deployment_gpt4_2):
    """exploration_ratio=1.0 -> always random."""
    random.seed(42)
    state = LocalRouterState()
    # Pre-populate dep_gpt4_1 with lower latency
    state.on_success("dep_gpt4_1", latency=0.1, tokens=100)
    state.on_success("dep_gpt4_2", latency=10.0, tokens=100)
    strategy = LowestLatencyStrategy(state=state, exploration_ratio=1.0)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    # Should pick randomly, not by latency
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


def test_lowest_latency_on_success(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = LowestLatencyStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens["dep_gpt4_1"] == 100


def test_lowest_latency_on_failure(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = LowestLatencyStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures["dep_gpt4_1"] == 1


# ======== TagBasedStrategy ========

@pytest.mark.asyncio
async def test_tag_based_from_request_tags(deployment_gpt4_1, deployment_gpt4_2):
    strategy = TagBasedStrategy()
    ctx = RoutingContext(model="gpt-4", messages=[], request_tags=["us-east"])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected.id == "dep_gpt4_1"  # us-east matches dep_gpt4_1


@pytest.mark.asyncio
async def test_tag_based_from_kwargs(deployment_gpt4_1, deployment_gpt4_2):
    strategy = TagBasedStrategy()
    ctx = RoutingContext(model="gpt-4", messages=[], kwargs={"tags": ["eu-west"]})
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected.id == "dep_gpt4_2"  # eu-west matches dep_gpt4_2


@pytest.mark.asyncio
async def test_tag_based_no_explicit_tags(deployment_gpt4_1, deployment_gpt3):
    """No tags -> falls back to model name as tag. gpt-3.5-turbo doesn't match gpt-4 tags."""
    strategy = TagBasedStrategy()
    ctx = RoutingContext(model="gpt-3.5-turbo", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt3]
    # gpt-3.5-turbo as tag doesn't match dep_gpt4_1's tags or dep_gpt3's tags
    # So falls back to all available
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_tag_based_no_match_falls_back(deployment_gpt4_1, deployment_gpt4_2):
    """No tag match should fall back to all available."""
    strategy = TagBasedStrategy()
    ctx = RoutingContext(model="gpt-4", messages=[], request_tags=["nonexistent"])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_tag_based_empty_deps():
    strategy = TagBasedStrategy()
    ctx = RoutingContext(model="m", messages=[])
    assert await strategy.select_deployment([], ctx) is None


@pytest.mark.asyncio
async def test_tag_based_custom_fallback(deployment_gpt4_1, deployment_gpt4_2):
    """Custom fallback strategy delegates selection."""
    state = LocalRouterState()
    strategy = TagBasedStrategy(fallback_strategy=LowestLatencyStrategy(state=state))
    ctx = RoutingContext(model="gpt-4", messages=[], request_tags=["nonexistent"])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


def test_tag_based_on_success_on_failure(deployment_gpt4_1):
    fallback_mock = MagicMock()
    strategy = TagBasedStrategy(fallback_strategy=fallback_mock)
    strategy.on_success(deployment_gpt4_1, latency=0.1, tokens=10)
    fallback_mock.on_success.assert_called_once_with(deployment_gpt4_1, 0.1, 10)
    error = ValueError("x")
    strategy.on_failure(deployment_gpt4_1, error)
    fallback_mock.on_failure.assert_called_once_with(deployment_gpt4_1, error)


# ======== TokenAwareStrategy ========

@pytest.mark.asyncio
async def test_token_aware_select_threshold(deployment_gpt4_1, deployment_gpt4_2):
    state = LocalRouterState()
    strategy = TokenAwareStrategy(state=state, exploration_ratio=0.0, token_threshold=1000)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    # Both have inf remaining (no token usage recorded), so both satisfy threshold
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_token_aware_select_below_threshold(deployment_gpt4_1, deployment_gpt4_2):
    state = LocalRouterState()
    # Simulate both near exhaustion
    state.token_usage["dep_gpt4_1"] = TokenUsage(limit=100, used=90)
    state.token_usage["dep_gpt4_2"] = TokenUsage(limit=100, used=95)
    strategy = TokenAwareStrategy(state=state, exploration_ratio=0.0, token_threshold=100)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    # Should pick dep with highest remaining (dep_gpt4_1 = 10)
    assert selected.id == "dep_gpt4_1"


@pytest.mark.asyncio
async def test_token_aware_empty_deps():
    state = LocalRouterState()
    strategy = TokenAwareStrategy(state=state)
    ctx = RoutingContext(model="m", messages=[])
    assert await strategy.select_deployment([], ctx) is None


def test_token_aware_on_success(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = TokenAwareStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens["dep_gpt4_1"] == 100


def test_token_aware_on_failure(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = TokenAwareStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures["dep_gpt4_1"] == 1


# ======== RateLimitAwareStrategy ========

@pytest.mark.asyncio
async def test_rate_limit_select_threshold(deployment_gpt4_1, deployment_gpt4_2):
    state = LocalRouterState()
    strategy = RateLimitAwareStrategy(state=state, exploration_ratio=0.0, rpm_threshold=10)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_rate_limit_empty_deps():
    state = LocalRouterState()
    strategy = RateLimitAwareStrategy(state=state)
    ctx = RoutingContext(model="m", messages=[])
    assert await strategy.select_deployment([], ctx) is None


def test_rate_limit_on_success(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = RateLimitAwareStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens["dep_gpt4_1"] == 100


def test_rate_limit_on_failure(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = RateLimitAwareStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures["dep_gpt4_1"] == 1


# ======== AdaptiveStrategy ========

def test_calculate_score_healthy_inf(deployment_gpt4_1):
    """All inf scores produce max values."""
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    score = strategy._calculate_score(deployment_gpt4_1, time.time())
    # health=1.0, token=1.0 (inf), rpm=1.0 (inf), latency=0.0 (inf -> 0.0)
    expected = 1.0 * 1.0 + 0.5 * 1.0 + 0.3 * 1.0 + 0.2 * 0.0
    assert score == pytest.approx(expected)


def test_calculate_score_token_ratio(deployment_gpt4_1):
    state = LocalRouterState()
    state.token_usage["dep_gpt4_1"] = TokenUsage(limit=1000, used=500)
    strategy = AdaptiveStrategy(state=state, token_threshold=1000)
    score = strategy._calculate_score(deployment_gpt4_1, time.time())
    # token_score = min(1.0, 500/1000) = 0.5
    assert score > 0


def test_calculate_score_unhealthy(deployment_gpt4_1):
    state = LocalRouterState()
    state.health_state["dep_gpt4_1"] = False
    strategy = AdaptiveStrategy(state=state)
    score = strategy._calculate_score(deployment_gpt4_1, time.time())
    # health_score=0.0 drives score down significantly
    assert 0 <= score < 2.0  # still has token/rpm/latency contributions


@pytest.mark.asyncio
async def test_adaptive_select(deployment_gpt4_1, deployment_gpt4_2):
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state, exploration_ratio=0.0)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_adaptive_empty_deps():
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    ctx = RoutingContext(model="m", messages=[])
    assert await strategy.select_deployment([], ctx) is None


@pytest.mark.asyncio
async def test_adaptive_session_affinity(deployment_gpt4_1, deployment_gpt4_2):
    """Session affinity returns cached deployment."""
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state, exploration_ratio=0.0)
    now = time.time()
    strategy._update_session_mapping("session_xyz", "dep_gpt4_1", now)
    ctx = RoutingContext(model="gpt-4", messages=[], kwargs={"session_id": "session_xyz"})
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected.id == "dep_gpt4_1"


@pytest.mark.asyncio
async def test_adaptive_session_affinity_expired(deployment_gpt4_1, deployment_gpt4_2):
    """Session mapping expired -> falls back to normal selection."""
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state, exploration_ratio=0.0)
    # Set very old timestamp
    old_time = time.time() - 3600
    strategy._update_session_mapping("session_old", "dep_gpt4_1", old_time)
    ctx = RoutingContext(model="gpt-4", messages=[], kwargs={"session_id": "session_old"})
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    # After cleanup, affinity will miss
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


@pytest.mark.asyncio
async def test_adaptive_session_not_in_available(deployment_gpt4_1, deployment_gpt4_2, deployment_gpt3):
    """Cached deployment not in available list -> normal selection."""
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state, exploration_ratio=0.0)
    strategy._update_session_mapping("s1", "dep_gpt3", time.time())
    ctx = RoutingContext(model="gpt-4", messages=[], kwargs={"session_id": "s1"})
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    # dep_gpt3 is not in the available list, so should fall through
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps


def test_get_session_affinity_no_mapping(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    result = strategy._get_session_affinity_deployment("unknown", [deployment_gpt4_1], time.time())
    assert result is None


def test_get_session_affinity_hit(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    strategy._update_session_mapping("s1", "dep_gpt4_1", time.time())
    result = strategy._get_session_affinity_deployment("s1", [deployment_gpt4_1], time.time())
    assert result.id == "dep_gpt4_1"


def test_get_session_affinity_cleanup_trigger(deployment_gpt4_1):
    """Lazy cleanup triggers when interval has passed."""
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state, session_cleanup_interval=0.001)
    strategy._update_session_mapping("expired_sess", "dep_gpt4_1", time.time() - 3600)
    # Set cleanup time far in past to trigger cleanup
    strategy._last_cleanup_time = time.time() - 10
    result = strategy._get_session_affinity_deployment(
        "expired_sess", [deployment_gpt4_1], time.time()
    )
    assert result is None  # expired


def test_cleanup_expired_sessions():
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    now = time.time()
    strategy._update_session_mapping("fresh", "d1", now)
    strategy._update_session_mapping("stale", "d2", now - 3600)
    strategy._cleanup_expired_sessions(now)
    assert state.session_deployment_map.get("fresh") == "d1"
    assert state.session_deployment_map.get("stale") is None


def test_adaptive_on_success(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens["dep_gpt4_1"] == 100


def test_adaptive_on_failure(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures["dep_gpt4_1"] == 1


@pytest.mark.asyncio
async def test_adaptive_exploration(deployment_gpt4_1, deployment_gpt4_2):
    """exploration_ratio=1.0 -> always random."""
    random.seed(42)
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state, exploration_ratio=1.0)
    ctx = RoutingContext(model="gpt-4", messages=[])
    deps = [deployment_gpt4_1, deployment_gpt4_2]
    selected = await strategy.select_deployment(deps, ctx)
    assert selected in deps
