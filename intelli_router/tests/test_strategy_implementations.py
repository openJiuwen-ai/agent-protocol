"""Tests for all strategy implementations and the create_strategy factory."""
import time
import random
import pytest
from unittest.mock import AsyncMock, MagicMock
from intelli_router.strategy import (
    create_strategy, SimpleShuffleStrategy, LowestLatencyStrategy,
    TagBasedStrategy, OrderedFailoverStrategy, TagFilteredStrategy,
    TokenAwareStrategy, RateLimitAwareStrategy,
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


def test_create_ordered_failover():
    s = create_strategy("ordered-failover")
    assert isinstance(s, OrderedFailoverStrategy)


def test_create_tag_filtered():
    s = create_strategy("tag-filtered", fallback_tag="paid")
    assert isinstance(s, TagFilteredStrategy)


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


def test_lowest_latency_on_success_noop(deployment_gpt4_1):
    """策略回调不再直接更新 state（router 层统一更新）。"""
    state = LocalRouterState()
    strategy = LowestLatencyStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens == {}


def test_lowest_latency_on_failure_noop(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = LowestLatencyStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures == {}


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


# ======== OrderedFailoverStrategy / TagFilteredStrategy ========

@pytest.mark.asyncio
async def test_ordered_failover_selects_first_available(deployment_gpt4_1, deployment_gpt4_2):
    strategy = OrderedFailoverStrategy()
    ctx = RoutingContext(model="gpt-4", messages=[])
    selected = await strategy.select_deployment([deployment_gpt4_1, deployment_gpt4_2], ctx)
    assert selected.id == deployment_gpt4_1.id


@pytest.mark.asyncio
async def test_tag_filtered_matches_fallback_tag(deployment_gpt4_1, deployment_gpt4_2):
    deployment_gpt4_1.fallback_tag = "cheap-model"
    deployment_gpt4_2.fallback_tag = "paid-model"
    deployment_gpt4_2.model_description = "good for legal knowledge"
    strategy = TagFilteredStrategy(fallback_tag="paid-model")
    ctx = RoutingContext(model="gpt-4", messages=[])
    selected = await strategy.select_deployment([deployment_gpt4_1, deployment_gpt4_2], ctx)
    assert selected.id == "dep_gpt4_2"
    assert selected.model_description == "good for legal knowledge"


@pytest.mark.asyncio
async def test_tag_filtered_no_match_returns_none(deployment_gpt4_1, deployment_gpt4_2):
    deployment_gpt4_1.fallback_tag = "cheap-model"
    deployment_gpt4_2.fallback_tag = "paid-model"
    strategy = TagFilteredStrategy(fallback_tag="not-present")
    ctx = RoutingContext(model="gpt-4", messages=[])
    selected = await strategy.select_deployment([deployment_gpt4_1, deployment_gpt4_2], ctx)
    assert selected is None


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


def test_token_aware_on_success_noop(deployment_gpt4_1):
    """策略回调不再直接更新 state（router 层统一更新）。"""
    state = LocalRouterState()
    strategy = TokenAwareStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens == {}


def test_token_aware_on_failure_noop(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = TokenAwareStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures == {}


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


def test_rate_limit_on_success_noop(deployment_gpt4_1):
    """策略回调不再直接更新 state（router 层统一更新）。"""
    state = LocalRouterState()
    strategy = RateLimitAwareStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens == {}


def test_rate_limit_on_failure_noop(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = RateLimitAwareStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures == {}


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


def test_adaptive_on_success_noop(deployment_gpt4_1):
    """策略回调不再直接更新 state（router 层统一更新）。"""
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    strategy.on_success(deployment_gpt4_1, latency=0.5, tokens=100)
    assert state.total_tokens == {}


def test_adaptive_on_failure_noop(deployment_gpt4_1):
    state = LocalRouterState()
    strategy = AdaptiveStrategy(state=state)
    strategy.on_failure(deployment_gpt4_1, ValueError("x"))
    assert state.consecutive_failures == {}


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


# ======== tpm/rpm wiring integration (review 4, P0) ========

class TestQuotaWiringStrategyIntegration:
    """带 tpm/rpm 的部署经 ReliableRouter 注册后，配额感知策略的排序不再失效。

    修复前：TokenUsage()/RPMTracker() limit 恒 0，已请求过的部署 remaining
    恒 0，而未预热（无条目）部署 remaining 为 inf——TokenAware/RateLimitAware
    系统性地把"从未用过的部署"排在"刚用过一次的部署"前面，配额评分反向。
    """

    def _router(self, strategy, deployments):
        from intelli_router.router.reliable_router import ReliableRouter
        return ReliableRouter(deployments=deployments, strategy=strategy)

    @pytest.mark.asyncio
    async def test_token_aware_prefers_warmed_up_over_smaller_quota(self):
        """已预热的大配额部署 remaining > 0 且优于小配额部署。"""
        from intelli_router.core.deployment import Deployment
        big = Deployment(
            id="dep_big", model_name="m", api_key="k", api_base="b", tpm=100000
        )
        small = Deployment(
            id="dep_small", model_name="m", api_key="k", api_base="b", tpm=100
        )
        router = self._router("token-aware", [big, small])
        # 模拟 big 已成功处理一个请求（消耗部分配额）
        router.state.on_success("dep_big", latency=0.1, tokens=50)

        # 修复前：dep_big 的 TokenUsage limit=0 → remaining=0，
        # 策略会把 dep_small（甚至未预热 inf）排在前面。
        assert router.state.get_token_remaining("dep_big") == 100000 - 50

        ctx = RoutingContext(model="m", messages=[])
        strategy = router.strategy
        strategy.exploration_ratio = 0.0
        selected = await strategy.select_deployment([big, small], ctx)
        assert selected.id == "dep_big"

    @pytest.mark.asyncio
    async def test_rate_limit_aware_remaining_positive_after_use(self):
        """RPM 已用一次后 remaining 仍 > 0 且按配额排序。"""
        from intelli_router.core.deployment import Deployment
        big = Deployment(
            id="dep_big", model_name="m", api_key="k", api_base="b", rpm=1000
        )
        small = Deployment(
            id="dep_small", model_name="m", api_key="k", api_base="b", rpm=2
        )
        router = self._router("rate-limit-aware", [big, small])
        router.state.on_success("dep_big", latency=0.1, tokens=10)

        # 修复前 remaining 恒 0
        assert router.state.get_rpm_remaining("dep_big") == 1000 - 1

        ctx = RoutingContext(model="m", messages=[])
        strategy = router.strategy
        strategy.exploration_ratio = 0.0
        selected = await strategy.select_deployment([big, small], ctx)
        assert selected.id == "dep_big"

    @pytest.mark.asyncio
    async def test_adaptive_quota_scores_not_always_zero(self):
        """Adaptive 的 token/rpm 评分在接线后产生区分度。"""
        from intelli_router.core.deployment import Deployment
        dep = Deployment(
            id="dep_ad", model_name="m", api_key="k", api_base="b",
            tpm=10000, rpm=1000,
        )
        router = self._router("adaptive", [dep])
        router.state.on_success("dep_ad", latency=0.1, tokens=1000)

        strategy = router.strategy
        # token/rpm remaining 为有限正值（修复前 limit=0 → remaining=0 → 评分恒 0）
        token_remaining = router.state.get_token_remaining("dep_ad")
        rpm_remaining = router.state.get_rpm_remaining("dep_ad")
        assert 0 < token_remaining < float('inf')
        assert 0 < rpm_remaining < float('inf')
        # token_score = min(1.0, 9000/1000) = 1.0, rpm_score = min(1.0, 999/10) = 1.0
        expected_token_score = min(1.0, token_remaining / strategy.token_threshold)
        expected_rpm_score = min(1.0, rpm_remaining / strategy.rpm_threshold)
        score = strategy._calculate_score(dep, time.time())
        expected = (
            strategy.w_health * 1.0
            + strategy.w_token * expected_token_score
            + strategy.w_rpm * expected_rpm_score
            + strategy.w_latency * max(0.0, 1.0 - router.state.get_average_latency("dep_ad"))
        )
        assert score == pytest.approx(expected)
        # 配额贡献为正（修复前两项恒为 0）
        assert expected_token_score > 0
        assert expected_rpm_score > 0

    @pytest.mark.asyncio
    async def test_unconfigured_quota_keeps_inf_advantage(self):
        """未配置配额的部署保持 inf（不设限），与已配置部署并存时排序合理。"""
        from intelli_router.core.deployment import Deployment
        unconfigured = Deployment(
            id="dep_inf", model_name="m", api_key="k", api_base="b"
        )
        nearly_exhausted = Deployment(
            id="dep_low", model_name="m", api_key="k", api_base="b", tpm=100
        )
        router = self._router("token-aware", [unconfigured, nearly_exhausted])
        router.state.on_success("dep_low", latency=0.1, tokens=95)

        # inf（未配置）确实优于剩余 5（快耗尽）——这是正确的排序方向
        assert router.state.get_token_remaining("dep_low") == 5
        assert router.state.get_token_remaining("dep_inf") == float('inf')

        ctx = RoutingContext(model="m", messages=[])
        strategy = router.strategy
        strategy.exploration_ratio = 0.0
        selected = await strategy.select_deployment(
            [unconfigured, nearly_exhausted], ctx
        )
        assert selected.id == "dep_inf"
