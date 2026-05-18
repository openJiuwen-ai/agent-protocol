"""Tests for intelli_router.router.base_router."""
import httpx
import pytest
from unittest.mock import patch, AsyncMock, MagicMock
from intelli_router.router.base_router import BaseRouter
from intelli_router.core.deployment import Deployment
from intelli_router.utils.exceptions import (
    NoDeploymentAvailable, RouterError,
    DeploymentTimeoutError, DeploymentAuthError, DeploymentRateLimitError,
    DeploymentServerError, DeploymentNetworkError, DeploymentError,
)


@pytest.fixture
def base_router(sample_deployments):
    return BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)


# -------- model indices --------

def test_build_model_indices(base_router):
    indices = base_router.model_indices
    assert "gpt-4" in indices
    assert "gpt-3.5-turbo" in indices
    assert len(indices["gpt-4"]) == 3  # dep_gpt4_1, dep_gpt4_2, dep_cooldown
    assert len(indices["gpt-3.5-turbo"]) == 1


def test_get_deployments_for_model(base_router):
    deps = base_router.get_deployments_for_model("gpt-4")
    assert len(deps) == 3
    assert all(d.model_name == "gpt-4" for d in deps)


def test_get_deployments_for_unknown_model(base_router):
    assert base_router.get_deployments_for_model("unknown") == []


def test_get_model_list(base_router):
    models = base_router.get_model_list()
    assert "gpt-4" in models
    assert "gpt-3.5-turbo" in models


def test_get_deployment_configs(base_router):
    configs = base_router.get_deployment_configs()
    assert len(configs) == 4
    for cfg in configs:
        assert "id" in cfg
        assert "model_name" in cfg
        assert "api_base" in cfg
        assert "api_key" in cfg


def test_get_deployment_config_by_model(base_router):
    configs = base_router.get_deployment_config_by_model("gpt-4")
    assert len(configs) == 3
    assert all(c["model_name"] == "gpt-4" for c in configs)


# -------- _ensure_client --------

def test_ensure_client_lazy_creation(base_router):
    assert base_router._client is None
    client = base_router._ensure_client()
    assert client is not None
    # second call returns same
    assert base_router._ensure_client() is client


# -------- _make_request --------

@pytest.mark.asyncio
async def test_make_request_success(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {"choices": [{"text": "hello"}]}
        mock_client.post.return_value = mock_response

        # Re-create client after patch
        base_router._client = None
        result = await base_router._make_request(deployment_gpt4_1, {"model": "gpt-4"})
        assert result == {"choices": [{"text": "hello"}]}
        mock_client.post.assert_called_once()


@pytest.mark.asyncio
async def test_make_request_timeout(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_client.post.side_effect = httpx.TimeoutException("timeout", request=MagicMock())

        base_router._client = None
        with pytest.raises(DeploymentTimeoutError):
            await base_router._make_request(deployment_gpt4_1, {})


@pytest.mark.asyncio
async def test_make_request_auth_401(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        resp = MagicMock(spec=httpx.Response)
        resp.status_code = 401
        resp.text = ""
        mock_client.post.side_effect = httpx.HTTPStatusError("401", request=MagicMock(), response=resp)

        base_router._client = None
        with pytest.raises(DeploymentAuthError):
            await base_router._make_request(deployment_gpt4_1, {})


@pytest.mark.asyncio
async def test_make_request_auth_403(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        resp = MagicMock(spec=httpx.Response)
        resp.status_code = 403
        resp.text = ""
        mock_client.post.side_effect = httpx.HTTPStatusError("403", request=MagicMock(), response=resp)

        base_router._client = None
        with pytest.raises(DeploymentAuthError):
            await base_router._make_request(deployment_gpt4_1, {})


@pytest.mark.asyncio
async def test_make_request_rate_limit(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        resp = MagicMock(spec=httpx.Response)
        resp.status_code = 429
        resp.text = ""
        resp.headers = {"retry-after": "30"}
        mock_client.post.side_effect = httpx.HTTPStatusError("429", request=MagicMock(), response=resp)

        base_router._client = None
        with pytest.raises(DeploymentRateLimitError) as exc:
            await base_router._make_request(deployment_gpt4_1, {})
        assert exc.value.details.get("retry_after") == 30.0


@pytest.mark.asyncio
async def test_make_request_server_error(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        resp = MagicMock(spec=httpx.Response)
        resp.status_code = 502
        resp.text = "bad gateway"
        mock_client.post.side_effect = httpx.HTTPStatusError("502", request=MagicMock(), response=resp)

        base_router._client = None
        with pytest.raises(DeploymentServerError) as exc:
            await base_router._make_request(deployment_gpt4_1, {})
        assert exc.value.details.get("status_code") == 502
        assert "bad gateway" in exc.value.details.get("response_body", "")


@pytest.mark.asyncio
async def test_make_request_connect_error(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_client.post.side_effect = httpx.ConnectError("connection refused")

        base_router._client = None
        with pytest.raises(DeploymentNetworkError):
            await base_router._make_request(deployment_gpt4_1, {})


@pytest.mark.asyncio
async def test_make_request_remote_protocol_error(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_client.post.side_effect = httpx.RemoteProtocolError("protocol error")

        base_router._client = None
        with pytest.raises(DeploymentNetworkError):
            await base_router._make_request(deployment_gpt4_1, {})


@pytest.mark.asyncio
async def test_make_request_generic_http_error(base_router, deployment_gpt4_1):
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        # httpx.HTTPError that doesn't match other cases (e.g., StreamError)
        mock_client.post.side_effect = httpx.HTTPError("generic error")

        base_router._client = None
        with pytest.raises(DeploymentError):
            await base_router._make_request(deployment_gpt4_1, {})


# -------- completion --------

@pytest.mark.asyncio
async def test_completion_with_specified_deployment(base_router, deployment_gpt4_1):
    with patch.object(base_router, '_make_request', new=AsyncMock(return_value={"ok": True})) as mock_req:
        result = await base_router.completion(
            "gpt-4", [{"role": "user", "content": "hi"}],
            deployment=deployment_gpt4_1,
        )
        assert result == {"ok": True}
        mock_req.assert_called_once()


@pytest.mark.asyncio
async def test_completion_no_deployment_specified(base_router, deployment_gpt4_1):
    with patch.object(base_router, '_make_request', new=AsyncMock(return_value={"ok": True})) as mock_req:
        result = await base_router.completion(
            "gpt-4", [{"role": "user", "content": "hi"}],
        )
        assert result == {"ok": True}
        mock_req.assert_called_once()


@pytest.mark.asyncio
async def test_completion_model_not_found(base_router):
    with pytest.raises(NoDeploymentAvailable):
        await base_router.completion("unknown-model", [{"role": "user", "content": "hi"}])


# -------- completion_with_fallback --------

@pytest.mark.asyncio
async def test_fallback_primary_succeeds(base_router):
    with patch.object(base_router, 'completion', new=AsyncMock(return_value={"ok": True})) as mock_comp:
        result = await base_router.completion_with_fallback(
            "gpt-4", [{"role": "user", "content": "hi"}],
            fallback={"gpt-4": "gpt-3.5-turbo"},
        )
        assert result == {"ok": True}
        mock_comp.assert_called_once()


@pytest.mark.asyncio
async def test_fallback_primary_fails_fallback_succeeds(base_router):
    call_count = 0

    async def mock_completion(model, messages, deployment=None, **kwargs):
        nonlocal call_count
        call_count += 1
        if call_count == 1:
            raise ValueError("primary failed")
        return {"ok": True, "model": model}

    with patch.object(base_router, 'completion', new=mock_completion):
        result = await base_router.completion_with_fallback(
            "gpt-4", [{"role": "user", "content": "hi"}],
            fallback={"gpt-4": "gpt-3.5-turbo"},
        )
        assert result == {"ok": True, "model": "gpt-4"}


@pytest.mark.asyncio
async def test_fallback_all_fail(base_router):
    with patch.object(base_router, 'completion',
                      new=AsyncMock(side_effect=ValueError("fail"))):
        with pytest.raises(RouterError):
            await base_router.completion_with_fallback(
                "gpt-4", [{"role": "user", "content": "hi"}],
                fallback={"gpt-4": "gpt-3.5-turbo"},
            )


@pytest.mark.asyncio
async def test_fallback_no_fallback_specified(base_router):
    with patch.object(base_router, 'completion', new=AsyncMock(return_value={"ok": True})):
        result = await base_router.completion_with_fallback(
            "gpt-4", [{"role": "user", "content": "hi"}],
        )
        assert result == {"ok": True}


# -------- close / context manager --------

@pytest.mark.asyncio
async def test_close(base_router):
    base_router._ensure_client()
    assert base_router._client is not None
    await base_router.close()
    assert base_router._client is None


@pytest.mark.asyncio
async def test_close_no_client(base_router):
    """close() should not raise when client is None."""
    assert base_router._client is None
    await base_router.close()


@pytest.mark.asyncio
async def test_async_context_manager(base_router):
    async with base_router as r:
        assert r is base_router
    assert base_router._client is None
