"""Tests for intelli_router.health.checker."""
import asyncio
import time
from unittest.mock import patch, AsyncMock, MagicMock
import pytest
from intelli_router.health.checker import SDKHealthChecker, HealthCheckResult
from intelli_router.core.deployment import Deployment, DeploymentStatus


@pytest.fixture
def health_deployments(deployment_gpt4_1, deployment_gpt4_2):
    return [deployment_gpt4_1, deployment_gpt4_2]


@pytest.fixture
def health_checker(health_deployments, router_state):
    return SDKHealthChecker(
        deployments=health_deployments,
        state=router_state,
        check_interval=300,
        check_timeout=5,
    )


def test_health_check_result_dataclass():
    result = HealthCheckResult(
        deployment_id="dep1",
        is_healthy=True,
        latency=0.5,
        error=None,
    )
    assert result.deployment_id == "dep1"
    assert result.is_healthy is True
    assert result.latency == 0.5
    assert result.error is None
    assert result.timestamp > 0


def _setup_check_client_mock(health_checker, side_effect):
    """Set up a mock _ensure_client that returns a client with the given
    post side_effect. Clears any previously cached client."""
    health_checker._client = None
    mock_client = AsyncMock()
    mock_client.post.side_effect = side_effect
    health_checker._ensure_client = MagicMock(return_value=mock_client)
    return mock_client


@pytest.mark.asyncio
async def test_check_deployment_success(health_checker, deployment_gpt4_1):
    mock_response = MagicMock()
    mock_response.status_code = 200
    _setup_check_client_mock(health_checker, [mock_response])

    result = await health_checker.check_deployment(deployment_gpt4_1)
    assert result.is_healthy is True
    assert result.deployment_id == deployment_gpt4_1.id
    assert result.latency is not None
    assert result.latency > 0
    assert result.error is None


@pytest.mark.asyncio
async def test_check_deployment_non_200(health_checker, deployment_gpt4_1):
    mock_response = MagicMock()
    mock_response.status_code = 500
    _setup_check_client_mock(health_checker, [mock_response])

    result = await health_checker.check_deployment(deployment_gpt4_1)
    assert result.is_healthy is False
    assert "HTTP 500" in result.error


@pytest.mark.asyncio
async def test_check_deployment_exception(health_checker, deployment_gpt4_1):
    _setup_check_client_mock(health_checker, ConnectionError("connection refused"))

    result = await health_checker.check_deployment(deployment_gpt4_1)
    assert result.is_healthy is False
    assert "connection refused" in result.error


@pytest.mark.asyncio
async def test_check_all_deployments(health_checker, health_deployments):
    mock_response = MagicMock()
    mock_response.status_code = 200
    _setup_check_client_mock(health_checker, [mock_response, mock_response])

    results = await health_checker.check_all_deployments()
    assert len(results) == len(health_deployments)
    for dep_id, result in results.items():
        assert isinstance(result, HealthCheckResult)
        assert result.is_healthy is True
    for dep in health_deployments:
        assert health_checker.state.health_state.get(dep.id) is True


@pytest.mark.asyncio
async def test_check_all_deployments_partial_failure(health_checker, health_deployments):
    mock_ok = MagicMock()
    mock_ok.status_code = 200
    _setup_check_client_mock(health_checker, [mock_ok, ConnectionError("timeout")])

    results = await health_checker.check_all_deployments()
    assert len(results) == 2
    assert results[health_deployments[0].id].is_healthy is True
    assert results[health_deployments[1].id].is_healthy is False


@pytest.mark.asyncio
async def test_gather_exception_handling(health_checker, health_deployments):
    """check_all_deployments handles exceptions from gather properly."""
    with patch.object(health_checker, 'check_deployment',
                      side_effect=ValueError("unexpected")):
        results = await health_checker.check_all_deployments()
        assert len(results) == len(health_deployments)
        for dep in health_deployments:
            assert results[dep.id].is_healthy is False


def test_get_healthy_deployments(health_checker, health_deployments):
    health_checker.state.health_state[health_deployments[0].id] = True
    health_checker.state.health_state[health_deployments[1].id] = False

    healthy = health_checker.get_healthy_deployments(time.time())
    assert len(healthy) == 1
    assert healthy[0].id == health_deployments[0].id


def test_get_unhealthy_ids(health_checker, health_deployments):
    health_checker.state.health_state[health_deployments[0].id] = True
    health_checker.state.health_state[health_deployments[1].id] = False

    unhealthy = health_checker.get_unhealthy_ids()
    assert health_deployments[0].id not in unhealthy
    assert health_deployments[1].id in unhealthy


@pytest.mark.asyncio
async def test_start_background_check(health_checker):
    await health_checker.start_background_check()
    assert health_checker._running is True
    assert health_checker._task is not None
    # Clean up
    await health_checker.stop_background_check()


@pytest.mark.asyncio
async def test_start_background_check_already_running(health_checker):
    await health_checker.start_background_check()
    task = health_checker._task
    await health_checker.start_background_check()  # should be no-op
    assert health_checker._task is task  # same task
    await health_checker.stop_background_check()


@pytest.mark.asyncio
async def test_stop_background_check(health_checker):
    await health_checker.start_background_check()
    await health_checker.stop_background_check()
    assert health_checker._running is False
    assert health_checker._task is None


@pytest.mark.asyncio
async def test_stop_background_check_not_running(health_checker):
    # Should not raise
    await health_checker.stop_background_check()


@pytest.mark.asyncio
async def test_background_loop(health_checker):
    """Background loop runs check_all_deployments and catches exceptions."""
    with patch.object(health_checker, 'check_all_deployments',
                      new=AsyncMock()) as mock_check:
        health_checker._running = True

        # Run the loop briefly, then stop
        async def run_loop():
            health_checker._task = asyncio.create_task(health_checker._background_loop())
            await asyncio.sleep(0.01)
            health_checker._running = False
            await asyncio.sleep(0)

        await run_loop()
        # Background loop will await sleep after check, and then exit because _running=False
        # Wait a brief moment
        await asyncio.sleep(0.05)
        mock_check.assert_called()
