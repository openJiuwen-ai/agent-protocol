"""Tests for intelli_router.health.checker."""
import asyncio
import json
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


def _fresh_state():
    """A fresh LocalRouterState for checkers built inside a single test."""
    from intelli_router.core.state import LocalRouterState
    return LocalRouterState()


def _setup_check_client_mock(health_checker, side_effect):
    """Set up a mock _ensure_client that returns a client with the given
    post side_effect. Clears any previously cached client."""
    health_checker._clients = {}
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


# -------- request signing + bytes body (review 7) --------

@pytest.mark.asyncio
async def test_check_deployment_signs_request_for_bedrock(health_deployments):
    """A signing provider (aws-bedrock) must have its health check request
    passed through adapter.sign_request, with signed headers on the wire."""
    dep = Deployment(
        id="dep_bedrock",
        model_name="anthropic.claude-3-haiku",
        api_key="AKIA-test:secret",
        api_base="https://bedrock-runtime.us-east-1.amazonaws.com",
        provider="aws-bedrock",
    )
    checker = SDKHealthChecker(
        deployments=[dep],
        state=_fresh_state(),
        check_interval=300,
        check_timeout=5,
    )
    mock_response = MagicMock()
    mock_response.status_code = 200
    mock_client = _setup_check_client_mock(checker, [mock_response])

    signed_headers = {
        "Content-Type": "application/json",
        "Authorization": "AWS4-HMAC-SHA256 Credential=AKIA-test/...",
        "X-Amz-Date": "20260920T000000Z",
    }
    mock_adapter = MagicMock(
        transform_request=MagicMock(return_value={"messages": []}),
        get_api_url=MagicMock(
            return_value="https://bedrock-runtime.us-east-1.amazonaws.com"
            "/model/anthropic.claude-3-haiku/converse"
        ),
        get_headers=MagicMock(return_value={"Content-Type": "application/json"}),
        sign_request=MagicMock(return_value=signed_headers),
    )
    with patch.object(
        checker,
        "_get_cached_adapter",
        MagicMock(return_value=mock_adapter),
    ):
        result = await checker.check_deployment(dep)

    assert result.is_healthy is True
    # sign_request was called before the post
    mock_adapter.sign_request.assert_called_once()
    sign_args = mock_adapter.sign_request.call_args.args
    assert sign_args[0] == "POST"  # method
    assert "converse" in sign_args[1]  # url
    # the signed headers are the ones actually sent
    post_kwargs = mock_client.post.call_args.kwargs
    assert post_kwargs["headers"] is signed_headers
    assert "Authorization" in post_kwargs["headers"]


@pytest.mark.asyncio
async def test_check_deployment_body_sent_as_bytes(health_checker, deployment_gpt4_1):
    """The request body must be passed as bytes via content= (not json=),
    so that SigV4 signs the exact bytes on the wire."""
    mock_response = MagicMock()
    mock_response.status_code = 200
    mock_client = _setup_check_client_mock(health_checker, [mock_response])

    await health_checker.check_deployment(deployment_gpt4_1)

    post_kwargs = mock_client.post.call_args.kwargs
    assert "json" not in post_kwargs, "health check must not use json= (breaks signing)"
    body = post_kwargs.get("content")
    assert isinstance(body, bytes)
    parsed = json.loads(body.decode("utf-8"))
    assert parsed["model"] == deployment_gpt4_1.model_name
    assert parsed["messages"] == health_checker.check_message


@pytest.mark.asyncio
async def test_check_deployment_signature_covers_sent_bytes(deployment_gpt4_1):
    """sign_request must receive exactly the bytes later sent via content=."""
    dep = deployment_gpt4_1
    checker = SDKHealthChecker(
        deployments=[dep],
        state=_fresh_state(),
        check_interval=300,
        check_timeout=5,
    )
    mock_response = MagicMock()
    mock_response.status_code = 200
    mock_client = _setup_check_client_mock(checker, [mock_response])

    captured = {}

    def fake_sign(method, url, headers, body, deployment):
        captured["body"] = body
        signed = dict(headers)
        signed["X-Signature"] = "sig"
        return signed

    real_adapter = checker._get_cached_adapter(dep.provider)
    with patch.object(
        real_adapter, "sign_request", side_effect=fake_sign
    ):
        result = await checker.check_deployment(dep)

    assert result.is_healthy is True
    post_kwargs = mock_client.post.call_args.kwargs
    assert captured["body"] is post_kwargs["content"]
    assert post_kwargs["headers"].get("X-Signature") == "sig"


# -------- verify_ssl client grouping in health checker (review 7) --------

def test_health_checker_client_grouped_by_verify(health_deployments):
    """Health checker caches one client per verify_ssl group."""
    checker = SDKHealthChecker(
        deployments=health_deployments,
        state=_fresh_state(),
        check_interval=300,
        check_timeout=5,
    )
    client_true = checker._ensure_client(True)
    client_false = checker._ensure_client(False)
    assert client_true is not client_false
    assert checker._ensure_client(True) is client_true
    assert checker._ensure_client(False) is client_false
    assert set(checker._clients.keys()) == {True, False}


@pytest.mark.asyncio
async def test_health_checker_uses_deployment_verify_ssl(health_deployments):
    """check_deployment selects the client group from deployment.verify_ssl."""
    dep = Deployment(
        id="dep_nossl",
        model_name="gpt-4",
        api_key="sk-test",
        api_base="https://self-signed.example.com",
        verify_ssl=False,
    )
    checker = SDKHealthChecker(
        deployments=[dep],
        state=_fresh_state(),
        check_interval=300,
        check_timeout=5,
    )
    mock_response = MagicMock()
    mock_response.status_code = 200

    created = {}

    def fake_client_cls(**kwargs):
        mock_client = AsyncMock()
        mock_client.post.side_effect = [mock_response]
        created["kwargs"] = kwargs
        created["client"] = mock_client
        return mock_client

    with patch("intelli_router.health.checker.httpx.AsyncClient", side_effect=fake_client_cls):
        result = await checker.check_deployment(dep)

    assert result.is_healthy is True
    assert created["kwargs"].get("verify") is False
    # the client was cached in the False group
    assert checker._clients.get(False) is created["client"]


@pytest.mark.asyncio
async def test_health_checker_close_closes_all_groups(health_deployments):
    """close() closes every cached client, not just the last used one."""
    checker = SDKHealthChecker(
        deployments=health_deployments,
        state=_fresh_state(),
        check_interval=300,
        check_timeout=5,
    )
    with patch("intelli_router.health.checker.httpx.AsyncClient") as mock_cls:
        clients = []
        for _verify in (True, False):
            mock_client = AsyncMock()
            mock_client.is_closed = False
            clients.append(mock_client)
        mock_cls.side_effect = clients
        checker._ensure_client(True)
        checker._ensure_client(False)

        await checker.close()
        for c in clients:
            c.aclose.assert_awaited_once()
        assert checker._clients == {}
        assert checker._client is None
