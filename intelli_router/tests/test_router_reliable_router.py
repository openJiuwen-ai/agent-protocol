"""Tests for intelli_router.router.reliable_router."""
import time
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
    """_get_available_deployments checks state.deployment_status, defaulting to HEALTHY."""
    deps = reliable_router._get_available_deployments("gpt-4")
    # dep_gpt4_1, dep_gpt4_2, dep_cooldown are all gpt-4
    # dep_cooldown is not in state.deployment_status yet, so defaults to HEALTHY
    assert len(deps) == 3


def test_get_available_cooldown_expired(reliable_router, deployment_gpt4_cooldown):
    """COOLDOWN past cooldown_until -> auto-recovered."""
    reliable_router.state.deployment_status[deployment_gpt4_cooldown.id] = DeploymentStatus.COOLDOWN
    reliable_router.state.cooldown_until[deployment_gpt4_cooldown.id] = time.time() - 10
    deps = reliable_router._get_available_deployments("gpt-4")
    assert deployment_gpt4_cooldown in deps
    assert reliable_router.state.deployment_status[deployment_gpt4_cooldown.id] == DeploymentStatus.HEALTHY


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
