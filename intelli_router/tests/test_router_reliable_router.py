"""Tests for intelli_router.router.reliable_router."""
import time
import threading
import pytest
from unittest.mock import patch, AsyncMock, MagicMock
from intelli_router.router.reliable_router import ReliableRouter
from intelli_router.strategy.simple_shuffle import SimpleShuffleStrategy
from intelli_router.strategy.adaptive import AdaptiveStrategy
from intelli_router.core.deployment import Deployment, DeploymentStatus
from intelli_router.utils.exceptions import RouterError, NoDeploymentAvailable


@pytest.fixture
def reliable_router(sample_deployments):
    """ReliableRouter with simple-shuffle strategy, 3 retries."""
    return ReliableRouter(
        deployments=sample_deployments,
        strategy="simple-shuffle",
        num_retries=3,
        timeout=30.0,
        cooldown_time=60.0,
        enable_health_check=False,
    )


@pytest.fixture
def reliable_router_with_health(sample_deployments):
    return ReliableRouter(
        deployments=sample_deployments,
        strategy="simple-shuffle",
        num_retries=0,
        timeout=30.0,
        enable_health_check=True,
        health_check_interval=300,
    )


def test_constructor_with_strategy_type(sample_deployments):
    """String strategy type -> created via factory."""
    router = ReliableRouter(deployments=sample_deployments, strategy="simple-shuffle")
    assert router.strategy is not None


def test_constructor_with_strategy_instance(sample_deployments):
    """RoutingStrategy instance -> used directly."""
    strategy = SimpleShuffleStrategy()
    router = ReliableRouter(deployments=sample_deployments, strategy=strategy)
    assert router.strategy is strategy


def test_constructor_adaptive_with_kwargs(sample_deployments):
    """strategy_kwargs passed through to strategy constructor."""
    router = ReliableRouter(
        deployments=sample_deployments,
        strategy="adaptive",
        w_health=2.0,
        w_token=1.0,
    )
    assert isinstance(router.strategy, AdaptiveStrategy)
    assert router.strategy.w_health == 2.0
    assert router.strategy.w_token == 1.0


def test_constructor_enable_health_check(sample_deployments):
    router = ReliableRouter(
        deployments=sample_deployments,
        enable_health_check=True,
        health_check_interval=300,
    )
    assert router.health_checker is not None
    assert router.health_checker.check_interval == 300


def test_constructor_disable_health_check(sample_deployments):
    router = ReliableRouter(deployments=sample_deployments, enable_health_check=False)
    assert router.health_checker is None


# -------- __aenter__ / __aexit__ --------

@pytest.mark.asyncio
async def test_aenter_starts_health_checker(reliable_router_with_health):
    router = reliable_router_with_health
    with patch.object(router.health_checker, 'start_background_check', new=AsyncMock()) as mock_start:
        await router.__aenter__()
        mock_start.assert_called_once()


@pytest.mark.asyncio
async def test_aenter_no_health_checker(reliable_router):
    """__aenter__ without health checker should not raise."""
    await reliable_router.__aenter__()


@pytest.mark.asyncio
async def test_aexit_stops_health_checker(reliable_router_with_health):
    router = reliable_router_with_health
    with patch.object(router.health_checker, 'stop_background_check', new=AsyncMock()) as mock_stop:
        await router.__aexit__(None, None, None)
        mock_stop.assert_called_once()


@pytest.mark.asyncio
async def test_aexit_no_health_checker(reliable_router):
    await reliable_router.__aexit__(None, None, None)


# -------- _get_available_deployments --------

def test_get_available_healthy(reliable_router):
    """_get_available_deployments checks state.deployment_status.

    fixture 中 dep_cooldown 自带 COOLDOWN 状态，构造 router 时会被同步
    进 state 作为初始状态（避免两套状态并存），因此被正确排除。
    """
    deps = reliable_router._get_available_deployments("gpt-4")
    # dep_gpt4_1, dep_gpt4_2 healthy; dep_cooldown 初始 COOLDOWN 被排除
    assert len(deps) == 2
    dep_ids = {d.id for d in deps}
    assert "dep_cooldown" not in dep_ids


def test_deployment_initial_cooldown_synced_to_state(reliable_router):
    """deployment 自带的 COOLDOWN 初始状态应同步进 state（附带修复）。"""
    assert reliable_router.state.deployment_status.get("dep_cooldown") == DeploymentStatus.COOLDOWN
    assert reliable_router.state.cooldown_until.get("dep_cooldown", 0) > time.time()


def test_get_available_cooldown_expired(reliable_router, deployment_gpt4_cooldown):
    """COOLDOWN past cooldown_until -> auto-recovered."""
    reliable_router.state.deployment_status[deployment_gpt4_cooldown.id] = DeploymentStatus.COOLDOWN
    reliable_router.state.cooldown_until[deployment_gpt4_cooldown.id] = time.time() - 10
    deps = reliable_router._get_available_deployments("gpt-4")
    assert deployment_gpt4_cooldown in deps
    assert reliable_router.state.deployment_status[deployment_gpt4_cooldown.id] == DeploymentStatus.HEALTHY


def test_get_available_cooldown_expired_restores_health_state(reliable_router, deployment_gpt4_cooldown):
    """issue #49: 软恢复时 health_state 必须一并恢复为 True。

    否则 COOLDOWN 到期后 health_state 残留 False，策略打分永远为 0，
    部署永远不会再被调度，也就永远不会再次进入 COOLDOWN。
    """
    dep_id = deployment_gpt4_cooldown.id
    reliable_router.state.deployment_status[dep_id] = DeploymentStatus.COOLDOWN
    reliable_router.state.cooldown_until[dep_id] = time.time() - 10
    reliable_router.state.health_state[dep_id] = False

    deps = reliable_router._get_available_deployments("gpt-4")
    assert deployment_gpt4_cooldown in deps
    assert reliable_router.state.deployment_status[dep_id] == DeploymentStatus.HEALTHY
    assert reliable_router.state.health_state[dep_id] is True


@pytest.mark.asyncio
async def test_soft_recovered_dead_deployment_can_reenter_cooldown(sample_deployments):
    """issue #49 端到端场景：dead 服务软恢复后，后续调用失败应能重新进入 COOLDOWN。"""
    # 用单部署 + 固定选择，模拟 openai-dead
    dead = Deployment(model_name="dead-model", api_key="k", api_base="b")
    router = ReliableRouter(deployments=[dead], strategy="simple-shuffle", num_retries=0)

    # 第1轮：请求失败 → COOLDOWN
    with patch.object(router, '_make_request', new=AsyncMock(side_effect=ValueError("dead"))):
        with pytest.raises(RouterError):
            await router.completion("dead-model", [{"role": "user", "content": "hi"}])
    assert router.state.deployment_status[dead.id] == DeploymentStatus.COOLDOWN
    assert router.state.health_state[dead.id] is False

    # 模拟 cooldown 到期
    router.state.cooldown_until[dead.id] = time.time() - 1

    # 第2轮：软恢复后再次调用，失败 → 应重新进入 COOLDOWN 而非停留在 healthy
    with patch.object(router, '_make_request', new=AsyncMock(side_effect=ValueError("still dead"))):
        with pytest.raises(RouterError):
            await router.completion("dead-model", [{"role": "user", "content": "hi"}])
    assert router.state.deployment_status[dead.id] == DeploymentStatus.COOLDOWN
    assert router.state.health_state[dead.id] is False
    # 退避递增：第2次失败 cooldown = 60 * 2
    assert router.state.cooldown_until[dead.id] >= time.time() + 60 * 2 - 1


def test_get_available_no_model(reliable_router):
    deps = reliable_router._get_available_deployments("nonexistent")
    assert deps == []


# -------- completion --------

@pytest.mark.asyncio
async def test_completion_success(reliable_router):
    mock_response = {
        "choices": [{"message": {"content": "ok"}}],
        "usage": {"completion_tokens": 10, "total_tokens": 20},
    }
    with patch.object(reliable_router, '_make_request', new=AsyncMock(return_value=mock_response)) as mock_req:
        result = await reliable_router.completion("gpt-4", [{"role": "user", "content": "hi"}])
        assert result == mock_response
        mock_req.assert_called_once()


@pytest.mark.asyncio
async def test_completion_failure_then_retry(reliable_router):
    """First attempt fails, second succeeds."""
    mock_response = {"choices": [], "usage": {"completion_tokens": 5}}
    _call_count = 0

    async def mock_make_request(deployment, request_body):
        nonlocal _call_count
        _call_count += 1
        if _call_count == 1:
            raise ValueError("first attempt failed")
        return mock_response

    with patch.object(reliable_router, '_make_request', new=mock_make_request):
        result = await reliable_router.completion("gpt-4", [{"role": "user", "content": "hi"}])
        assert result == mock_response
        assert _call_count == 2


@pytest.mark.asyncio
async def test_completion_all_fail(reliable_router):
    with patch.object(reliable_router, '_make_request', new=AsyncMock(side_effect=ValueError("fail"))):
        with pytest.raises(RouterError):
            await reliable_router.completion("gpt-4", [{"role": "user", "content": "hi"}])


@pytest.mark.asyncio
async def test_completion_no_available_deployments(reliable_router):
    """No deployments for model -> immediate NoDeploymentAvailable."""
    with pytest.raises(NoDeploymentAvailable):
        await reliable_router.completion("nonexistent", [{"role": "user", "content": "hi"}])


@pytest.mark.asyncio
async def test_completion_strategy_returns_none(reliable_router):
    """strategy returns None -> falls back to first available."""
    mock_response = {"choices": [], "usage": {"completion_tokens": 5}}

    class ReturnsNoneStrategy:
        async def select_deployment(self, deployments, context):
            return None
        def on_success(self, dep, latency, tokens):
            pass
        def on_failure(self, dep, error):
            pass

    reliable_router.strategy = ReturnsNoneStrategy()
    with patch.object(reliable_router, '_make_request', new=AsyncMock(return_value=mock_response)):
        result = await reliable_router.completion("gpt-4", [{"role": "user", "content": "hi"}])
        assert result == mock_response


@pytest.mark.asyncio
async def test_completion_zero_retries(reliable_router):
    """num_retries=0: first failure raises RouterError."""
    router = ReliableRouter(
        deployments=[reliable_router.deployments[0]],
        strategy="simple-shuffle",
        num_retries=0,
    )
    with patch.object(router, '_make_request', new=AsyncMock(side_effect=ValueError("fail"))):
        with pytest.raises(RouterError):
            await router.completion("gpt-4", [{"role": "user", "content": "hi"}])


# -------- batch_completion --------

@pytest.mark.asyncio
async def test_batch_completion(reliable_router):
    mock_response = {"choices": [], "usage": {"completion_tokens": 5}}

    with patch.object(reliable_router, 'completion',
                      new=AsyncMock(return_value=mock_response)) as mock_comp:
        requests = [
            {"model": "gpt-4", "messages": [{"role": "user", "content": "a"}]},
            {"model": "gpt-4", "messages": [{"role": "user", "content": "b"}]},
        ]
        results = await reliable_router.batch_completion(requests, max_concurrent=10)
        assert len(results) == 2
        assert mock_comp.call_count == 2


@pytest.mark.asyncio
async def test_batch_completion_partial_failures(reliable_router):
    _call_count = 0

    async def mock_completion(**kwargs):
        nonlocal _call_count
        _call_count += 1
        if _call_count == 1:
            return {"ok": True}
        raise ValueError("fail")

    with patch.object(reliable_router, 'completion', new=mock_completion):
        requests = [
            {"model": "gpt-4", "messages": [{"role": "user", "content": "a"}]},
            {"model": "gpt-4", "messages": [{"role": "user", "content": "b"}]},
        ]
        results = await reliable_router.batch_completion(requests, max_concurrent=10)
        assert len(results) == 2
        # gather with return_exceptions=True returns exceptions as values
        assert results[0] == {"ok": True}
        assert isinstance(results[1], ValueError)


# -------- update_deployments --------

def test_update_deployments(reliable_router, deployment_gpt3):
    new_deps = [deployment_gpt3]
    reliable_router.update_deployments(new_deps)
    assert len(reliable_router.deployments) == 1
    assert reliable_router.get_model_list() == ["gpt-3.5-turbo"]


def test_update_deployments_with_health_checker(reliable_router_with_health, deployment_gpt3):
    router = reliable_router_with_health
    router.update_deployments([deployment_gpt3])
    assert len(router.health_checker.deployments) == 1


# -------- get_stats --------

def test_get_stats(reliable_router):
    stats = reliable_router.get_stats()
    assert "total_deployments" in stats
    assert "model_list" in stats
    assert "deployment_status" in stats
    assert "consecutive_failures" in stats
    assert "latency_stats" in stats
    assert stats["total_deployments"] == 4


# -------- get_stats latency (issue #41/#42) --------

def test_get_stats_avg_latency_no_records_is_none(reliable_router):
    """issue #41: 无延迟记录时 avg_latency 应为 None 而非 inf。"""
    stats = reliable_router.get_stats()
    for dep_stats in stats["latency_stats"].values():
        assert dep_stats["avg_latency"] is None
        assert dep_stats["avg_normalized_latency"] is None


def test_get_stats_avg_latency_is_real_latency_rounded(reliable_router, deployment_gpt4_1):
    """issue #42: avg_latency 应为真实延迟（秒）且4位小数舍入，而非归一化延迟。"""
    # latency=0.5s, tokens=100 → normalized=0.005
    reliable_router.state.on_success(deployment_gpt4_1.id, latency=0.5, tokens=100)
    stats = reliable_router.get_stats()
    dep_stats = stats["latency_stats"][deployment_gpt4_1.id]
    assert dep_stats["avg_latency"] == 0.5
    assert dep_stats["avg_normalized_latency"] == 0.005


def test_get_stats_avg_latency_float_precision(reliable_router, deployment_gpt4_1):
    """issue #41: 浮点除法产生的长尾小数应被舍入到4位。"""
    # 1/3 会产生 0.3333333333333333 这样的长浮点
    reliable_router.state.on_success(deployment_gpt4_1.id, latency=1.0, tokens=1)
    reliable_router.state.on_success(deployment_gpt4_1.id, latency=0.5, tokens=1)
    reliable_router.state.on_success(deployment_gpt4_1.id, latency=0.25, tokens=1)
    stats = reliable_router.get_stats()
    avg = stats["latency_stats"][deployment_gpt4_1.id]["avg_latency"]
    assert avg == round(1.75 / 3, 4)
    # 不应出现 19 位小数
    assert len(str(avg).split(".")[-1]) <= 4


# -------- update_deployments 热替换并发 (issue #50) --------

def test_update_deployments_cleans_stale_state(reliable_router, deployment_gpt3):
    """热替换后已移除部署的 state 残留应被清理。"""
    old_dep = reliable_router.deployments[0]
    reliable_router.state.on_failure(old_dep.id, RuntimeError("x"))

    reliable_router.update_deployments([deployment_gpt3])

    assert old_dep.id not in reliable_router.state.deployment_status
    assert old_dep.id not in reliable_router.state.consecutive_failures
    assert old_dep.id not in reliable_router.state.cooldown_until


def test_build_model_indices_atomic(reliable_router, deployment_gpt3):
    """_build_model_indices 应一次性原子替换索引（issue #50 竞态根因）。"""
    original = reliable_router.model_indices
    reliable_router._build_model_indices()
    # 重建过程中不会出现"已清空"的中间态：构建前旧索引始终完整可读
    assert original is not reliable_router.model_indices


@pytest.mark.asyncio
async def test_hot_swap_concurrent_with_requests(sample_deployments):
    """issue #50 回归：请求循环执行期间热替换，不应出现间歇性失败。

    模拟：请求协程持续发起 completion，另一线程反复 update_deployments。
    修复前 _build_model_indices 先清空再填充，并发读会命中空索引窗口。
    """
    router = ReliableRouter(
        deployments=sample_deployments, strategy="simple-shuffle", num_retries=0,
    )
    mock_response = {"choices": [{"message": {"content": "ok"}}], "usage": {}}
    nodep_count = {"count": 0}

    async def mock_make_request(deployment, request_body):
        return mock_response

    router._make_request = mock_make_request

    stop = threading.Event()
    swap_errors = []

    def swapper():
        new_deps = [Deployment(model_name="gpt-4", api_key="k", api_base="b") for _ in range(4)]
        while not stop.is_set():
            try:
                router.update_deployments(new_deps)
            except Exception as e:  # pragma: no cover
                swap_errors.append(e)
                stop.set()

    t = threading.Thread(target=swapper, daemon=True)
    t.start()

    try:
        for _ in range(500):
            try:
                await router.completion("gpt-4", [{"role": "user", "content": "hi"}])
            except Exception as e:
                # 热替换窗口内 NoDeploymentAvailable 不应发生：
                # 新旧部署列表都包含 gpt-4
                if "No available deployment" in str(e):
                    nodep_count["count"] += 1
    finally:
        stop.set()
        t.join(timeout=2)

    assert not swap_errors
    assert nodep_count["count"] == 0, (
        f"热替换期间出现 {nodep_count['count']} 次 NoDeploymentAvailable"
    )


# -------- PR review fixes --------

def test_constructor_cooldown_without_until_no_crash():
    """P2-1 回归：COOLDOWN 且 cooldown_until=None 的 Deployment 构造 router
    不应抛 AttributeError（cooldown_time 须在同步循环前赋值）。"""
    from intelli_router.core.state import LocalRouterState
    dead = Deployment(
        model_name="m", api_key="k", api_base="b",
        status=DeploymentStatus.COOLDOWN, cooldown_until=None,
    )
    router = ReliableRouter(deployments=[dead])
    # 应使用 router 的 cooldown_time 计算默认冷却截止
    assert router.state.deployment_status[dead.id] == DeploymentStatus.COOLDOWN
    assert router.state.cooldown_until[dead.id] > time.time()


@pytest.mark.asyncio
async def test_legacy_strategy_no_double_counting():
    """P2-2 回归：按旧契约在回调内转发 state 更新的自定义策略，
    注入后不应造成 consecutive_failures 双重计数。"""
    from intelli_router.core.state import LocalRouterState
    from intelli_router.strategy.base_strategy import RoutingStrategy

    class LegacyStrategy(RoutingStrategy):
        def __init__(self, state):
            self.state = state
        async def select_deployment(self, deployments, context):
            return deployments[0] if deployments else None
        def on_success(self, deployment, latency, tokens):
            self.state.on_success(deployment.id, latency, tokens)
        def on_failure(self, deployment, error):
            self.state.on_failure(deployment.id, error)

    state = LocalRouterState()
    deps = [Deployment(model_name="m", api_key="k", api_base="b", id=f"d{i}") for i in range(2)]
    router = ReliableRouter(deployments=deps, strategy=LegacyStrategy(state))

    async def mock_make_request(deployment, request_body):
        raise ValueError("boom")
    router._make_request = mock_make_request

    with pytest.raises(RouterError):
        await router.completion("m", [{"role": "user", "content": "hi"}])

    # 每个部署只失败 1 次（4 次尝试里每部署被选 1 次后剔除）
    assert state.consecutive_failures == {"d0": 1, "d1": 1}


def test_update_deployments_shared_state_no_clobber():
    """P3 回归：多 router 共享同一 state 时，一方热替换不应清除
    另一方管理部署的运行时状态。"""
    from intelli_router.core.state import LocalRouterState
    from intelli_router.strategy.adaptive import AdaptiveStrategy

    shared_state = LocalRouterState()
    strategy = AdaptiveStrategy(state=shared_state)

    router_a = ReliableRouter(
        deployments=[Deployment(model_name="model-a", api_key="k", api_base="b", id="a1")],
        strategy=strategy,
    )
    router_b = ReliableRouter(
        deployments=[Deployment(model_name="model-b", api_key="k", api_base="b", id="b1")],
        strategy=strategy,
    )

    shared_state.on_failure("b1", RuntimeError("x"))
    assert shared_state.deployment_status["b1"] == DeploymentStatus.COOLDOWN

    # router_a 热替换自己的部署（不含 b1）
    router_a.update_deployments(
        [Deployment(model_name="model-a2", api_key="k", api_base="b", id="a2")]
    )

    # b1 的状态不应被误删
    assert shared_state.deployment_status.get("b1") == DeploymentStatus.COOLDOWN
    assert shared_state.consecutive_failures.get("b1") == 1
    # 旧部署 a1 的状态应被清理
    assert "a1" not in shared_state.deployment_status
    assert router_b.get_model_list() == ["model-b"]
