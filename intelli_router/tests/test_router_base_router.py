"""Tests for intelli_router.router.base_router."""
import threading
import time

import httpx
import pytest
from unittest.mock import patch, AsyncMock, MagicMock
from intelli_router.router.base_router import BaseRouter
from intelli_router.router.reliable_router import ReliableRouter
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
        assert "model_id" in cfg
        assert "model_name" in cfg
        assert "api_base" in cfg
        # api_key 明文不应泄露，只暴露是否已配置
        assert "api_key" not in cfg
        assert isinstance(cfg["has_api_key"], bool)
        assert cfg["has_api_key"] is True


def test_get_deployment_config_by_model(base_router):
    configs = base_router.get_deployment_config_by_model("gpt-4")
    assert len(configs) == 3
    assert all(c["model_name"] == "gpt-4" for c in configs)
    for c in configs:
        assert "api_key" not in c
        assert isinstance(c["has_api_key"], bool)
        assert c["has_api_key"] is True


# -------- concurrency: update_deployments vs get_deployments_for_model --------

def test_concurrent_update_and_get_deployments_for_model(sample_deployments):
    """Regression test for the index/list swap race.

    update_deployments() hot-swaps self.deployments and rebuilds
    self.model_indices. A reader that grabs the index and the list
    without the lock can mix the new list with the old index (or vice
    versa) and hit IndexError. Hammer both sides from multiple threads
    and assert no exception escapes.
    """
    router = ReliableRouter(
        deployments=sample_deployments,
        num_retries=0,
        enable_health_check=False,
    )
    # 缩短后的列表：只保留一个 gpt-4 部署，反复热替换 4 个 <-> 1 个
    short_list = [sample_deployments[0]]
    full_list = list(sample_deployments)

    stop = threading.Event()
    errors: list = []

    def updater():
        try:
            toggle = False
            while not stop.is_set():
                router.update_deployments(short_list if toggle else full_list)
                toggle = not toggle
        except Exception as e:  # pragma: no cover - only on regression
            errors.append(e)

    def reader():
        try:
            while not stop.is_set():
                deps = router.get_deployments_for_model("gpt-4")
                for d in deps:
                    assert d.model_name == "gpt-4"
        except Exception as e:  # pragma: no cover - only on regression
            errors.append(e)

    threads = [threading.Thread(target=updater)] + [
        threading.Thread(target=reader) for _ in range(4)
    ]
    for t in threads:
        t.start()
    time.sleep(1.0)
    stop.set()
    for t in threads:
        t.join(timeout=5)

    assert not errors, f"concurrent access raised: {errors}"
    # 热替换结束后读取仍应一致：最后一次是 short_list 或 full_list 之一
    deps = router.get_deployments_for_model("gpt-4")
    assert 1 <= len(deps) <= 3


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


# -------- completion availability filtering (review 5) --------

@pytest.mark.asyncio
async def test_completion_skips_state_cooldown_deployment(sample_deployments):
    """BaseRouter.completion 未显式指定 deployment 时，state 中标记 COOLDOWN
    （且仍在冷却期内）的部署应被跳过，取下一个可用部署。

    修复前：直接取 deployments[0]，运行期冷却状态（只存在于 state）被无视。
    """
    from intelli_router.core.deployment import DeploymentStatus

    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    first = sample_deployments[0]  # dep_gpt4_1：model_indices 中 gpt-4 的第一个
    # 运行期冷却只写 state（对象 status 仍是 HEALTHY）
    router.state.deployment_status[first.id] = DeploymentStatus.COOLDOWN
    router.state.cooldown_until[first.id] = time.time() + 3600

    requested = []

    async def mock_make_request(deployment, request_body):
        requested.append(deployment.id)
        return {"ok": True}

    with patch.object(router, '_make_request', new=mock_make_request):
        result = await router.completion("gpt-4", [{"role": "user", "content": "hi"}])

    assert result == {"ok": True}
    assert requested == [sample_deployments[1].id]  # dep_gpt4_2 被选中


@pytest.mark.asyncio
async def test_completion_all_state_cooldown_raises(sample_deployments):
    """全部部署在 state 中标记冷却 → NoDeploymentAvailable（而非盲目打第一个）。"""
    from intelli_router.core.deployment import DeploymentStatus

    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    for dep in sample_deployments:
        if dep.model_name != "gpt-4":
            continue
        router.state.deployment_status[dep.id] = DeploymentStatus.COOLDOWN
        router.state.cooldown_until[dep.id] = time.time() + 3600

    with patch.object(
        router, '_make_request', new=AsyncMock(return_value={"ok": True})
    ) as mock_req:
        with pytest.raises(NoDeploymentAvailable):
            await router.completion("gpt-4", [{"role": "user", "content": "hi"}])
        mock_req.assert_not_called()


@pytest.mark.asyncio
async def test_completion_state_cooldown_expired_is_available(sample_deployments):
    """state 中冷却已过期的部署不再被跳过（按当前时间判断）。"""
    from intelli_router.core.deployment import DeploymentStatus

    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    first = sample_deployments[0]
    # COOLDOWN 但冷却截止时间已过
    router.state.deployment_status[first.id] = DeploymentStatus.COOLDOWN
    router.state.cooldown_until[first.id] = time.time() - 10

    requested = []

    async def mock_make_request(deployment, request_body):
        requested.append(deployment.id)
        return {"ok": True}

    with patch.object(router, '_make_request', new=mock_make_request):
        await router.completion("gpt-4", [{"role": "user", "content": "hi"}])

    assert requested == [first.id]


@pytest.mark.asyncio
async def test_completion_explicit_deployment_bypasses_filter(
    base_router, deployment_gpt4_1
):
    """显式指定 deployment 的语义不变：调用方指定即使用（不做可用性过滤）。"""
    from intelli_router.core.deployment import DeploymentStatus

    base_router.state.deployment_status[deployment_gpt4_1.id] = DeploymentStatus.COOLDOWN
    base_router.state.cooldown_until[deployment_gpt4_1.id] = time.time() + 3600

    with patch.object(
        base_router, '_make_request', new=AsyncMock(return_value={"ok": True})
    ) as mock_req:
        result = await base_router.completion(
            "gpt-4", [{"role": "user", "content": "hi"}],
            deployment=deployment_gpt4_1,
        )
        assert result == {"ok": True}
        mock_req.assert_called_once()


# -------- acompletion_stream availability filtering (final review) --------

@pytest.mark.asyncio
async def test_acompletion_stream_skips_state_cooldown_deployment(sample_deployments):
    """BaseRouter.acompletion_stream 未显式指定 deployment 时，state 中标记
    COOLDOWN（且仍在冷却期内）的部署应被跳过，取下一个可用部署（与
    completion() 的过滤行为对称）。

    修复前：直接取 deployments[0]，运行期冷却状态（只存在于 state）被无视。
    """
    from intelli_router.core.deployment import DeploymentStatus

    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    first = sample_deployments[0]  # dep_gpt4_1：model_indices 中 gpt-4 的第一个
    # 运行期冷却只写 state（对象 status 仍是 HEALTHY）
    router.state.deployment_status[first.id] = DeploymentStatus.COOLDOWN
    router.state.cooldown_until[first.id] = time.time() + 3600

    requested = []

    class _StreamCtx:
        async def __aenter__(self):
            response = MagicMock()
            response.raise_for_status = MagicMock()
            return response

        async def __aexit__(self, *args):
            return False

    class _SingleChunkIter:
        def __aiter__(self):
            return self

        async def __anext__(self):
            raise StopAsyncIteration

    def make_adapter(dep):
        return MagicMock(
            get_api_url=MagicMock(return_value="https://api.example.com/v1"),
            get_headers=MagicMock(return_value={}),
            sign_request=MagicMock(side_effect=lambda m, u, h, b, d: h),
            transform_request=MagicMock(return_value={}),
            validate_request_config=MagicMock(return_value=None),
            iter_stream_events=MagicMock(return_value=_SingleChunkIter()),
        )

    def fake_get_adapter(dep):
        requested.append(dep.id)
        return make_adapter(dep)

    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = MagicMock()
        mock_client.stream = MagicMock(return_value=_StreamCtx())
        mock_cls.return_value = mock_client

        with patch.object(router, "_get_adapter", side_effect=fake_get_adapter):
            with patch.object(
                router, "_ensure_client", return_value=mock_client
            ):
                chunks = [
                    c async for c in router.acompletion_stream(
                        "gpt-4", [{"role": "user", "content": "hi"}]
                    )
                ]

    assert chunks == []
    # dep_gpt4_1 被跳过，dep_gpt4_2 被选中（dep_cooldown 的对象级 status
    # 已在 BaseRouter 构建索引时被忽略，state 中未登记默认 HEALTHY）
    assert requested == [sample_deployments[1].id]


@pytest.mark.asyncio
async def test_acompletion_stream_all_state_cooldown_raises(sample_deployments):
    """全部部署在 state 中标记冷却 → NoDeploymentAvailable（而非盲目打第一个）。"""
    from intelli_router.core.deployment import DeploymentStatus

    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    for dep in sample_deployments:
        if dep.model_name != "gpt-4":
            continue
        router.state.deployment_status[dep.id] = DeploymentStatus.COOLDOWN
        router.state.cooldown_until[dep.id] = time.time() + 3600

    with pytest.raises(NoDeploymentAvailable):
        async for _ in router.acompletion_stream(
            "gpt-4", [{"role": "user", "content": "hi"}]
        ):
            pass  # pragma: no cover - 不应产出任何 chunk


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


# -------- verify_ssl client grouping (review 6) --------

@pytest.mark.asyncio
async def test_ensure_client_verify_false_passed_to_client(sample_deployments):
    """A verify_ssl=False deployment must get a client created with verify=False."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    dep = Deployment(
        id="dep_nossl",
        model_name="gpt-4",
        api_key="sk-test",
        api_base="https://self-signed.example.com",
        verify_ssl=False,
    )
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {"choices": [{"text": "hi"}]}
        mock_client.post.return_value = mock_response

        await router._make_request(dep, {"model": "gpt-4"})
        # constructor was called with verify=False for the deployment's group
        mock_cls.assert_called_once()
        kwargs = mock_cls.call_args.kwargs
        assert kwargs.get("verify") is False


@pytest.mark.asyncio
async def test_ensure_client_verify_true_default(sample_deployments, deployment_gpt4_1):
    """A default (verify_ssl=True) deployment gets a client created with verify=True."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {"choices": [{"text": "hi"}]}
        mock_client.post.return_value = mock_response

        await router._make_request(deployment_gpt4_1, {"model": "gpt-4"})
        mock_cls.assert_called_once()
        kwargs = mock_cls.call_args.kwargs
        assert kwargs.get("verify") is True


def test_ensure_client_grouped_by_verify(sample_deployments):
    """verify_ssl=True and False deployments share one client per group,
    but the two groups hold distinct client instances."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    client_true = router._ensure_client(True)
    client_true_again = router._ensure_client(True)
    client_false = router._ensure_client(False)
    client_false_again = router._ensure_client(False)

    assert client_true is client_true_again
    assert client_false is client_false_again
    assert client_true is not client_false
    assert set(router._clients.keys()) == {True, False}


def test_ensure_client_default_arg_is_verify_true(sample_deployments):
    """_ensure_client() with no argument keeps the historical verify=True behavior."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    assert router._ensure_client() is router._clients[True]


@pytest.mark.asyncio
async def test_close_closes_all_verify_groups(sample_deployments):
    """close() must close every cached client, not just the last used one."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        clients = []
        for _verify in (True, False):
            mock_client = AsyncMock()
            mock_client.is_closed = False
            clients.append(mock_client)
        mock_cls.side_effect = clients
        router._ensure_client(True)
        router._ensure_client(False)

        await router.close()
        for c in clients:
            c.aclose.assert_awaited_once()
        assert router._clients == {}
        assert router._client is None


# -------- per-deployment timeout (review 8) --------

@pytest.mark.asyncio
async def test_make_request_deployment_timeout_overrides_router_timeout(
    sample_deployments,
):
    """deployment.timeout=5 wins over router timeout=30 and is passed to client.post."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    dep = Deployment(
        id="dep_fast",
        model_name="gpt-4",
        api_key="sk-test",
        api_base="https://api.example.com",
        timeout=5,
    )
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {"choices": [{"text": "hi"}]}
        mock_client.post.return_value = mock_response

        await router._make_request(dep, {"model": "gpt-4"})
        kwargs = mock_client.post.call_args.kwargs
        assert kwargs.get("timeout") == 5


@pytest.mark.asyncio
async def test_make_request_timeout_falls_back_to_router_timeout(
    sample_deployments, deployment_gpt4_1,
):
    """Without deployment.timeout, the router-level timeout applies per request."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {"choices": [{"text": "hi"}]}
        mock_client.post.return_value = mock_response

        await router._make_request(deployment_gpt4_1, {"model": "gpt-4"})
        kwargs = mock_client.post.call_args.kwargs
        assert kwargs.get("timeout") == 30.0


@pytest.mark.asyncio
async def test_make_request_timeout_error_reports_effective_timeout(
    sample_deployments,
):
    """DeploymentTimeoutError message/details carry the timeout actually in effect."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    dep = Deployment(
        id="dep_fast",
        model_name="gpt-4",
        api_key="sk-test",
        api_base="https://api.example.com",
        timeout=5,
    )
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client
        mock_client.post.side_effect = httpx.TimeoutException(
            "timeout", request=MagicMock()
        )

        with pytest.raises(DeploymentTimeoutError) as exc:
            await router._make_request(dep, {})
        assert "5s" in str(exc.value)
        assert exc.value.details.get("timeout") == 5


@pytest.mark.asyncio
async def test_stream_request_deployment_timeout_overrides_router_timeout(
    sample_deployments,
):
    """Stream path also applies per-deployment timeout and verify_ssl grouping."""
    router = BaseRouter(deployments=sample_deployments, num_retries=0, timeout=30.0)
    dep = Deployment(
        id="dep_fast",
        model_name="gpt-4",
        api_key="sk-test",
        api_base="https://api.example.com",
        timeout=5,
    )
    with patch("intelli_router.router.base_router.httpx.AsyncClient") as mock_cls:
        mock_client = AsyncMock()
        mock_cls.return_value = mock_client

        class _StreamCtx:
            async def __aenter__(self):
                response = MagicMock()
                response.raise_for_status = MagicMock()
                return response

            async def __aexit__(self, *args):
                return False

        # plain MagicMock: calling .stream(...) must synchronously return the ctx
        mock_client.stream = MagicMock(return_value=_StreamCtx())

        # empty async iterator: adapter.iter_stream_events yields nothing
        class _EmptyAsyncIter:
            def __aiter__(self):
                return self

            async def __anext__(self):
                raise StopAsyncIteration

        with patch.object(
            router, "_get_adapter", return_value=MagicMock(
                get_api_url=MagicMock(return_value="https://api.example.com/v1"),
                get_headers=MagicMock(return_value={}),
                sign_request=MagicMock(side_effect=lambda m, u, h, b, d: h),
                transform_request=MagicMock(return_value={}),
                validate_request_config=MagicMock(return_value=None),
                iter_stream_events=MagicMock(return_value=_EmptyAsyncIter()),
            )
        ):
            chunks = [
                c async for c in router.acompletion_stream(
                    "gpt-4", [{"role": "user", "content": "hi"}], deployment=dep
                )
            ]
        assert chunks == []
        kwargs = mock_client.stream.call_args.kwargs
        assert kwargs.get("timeout") == 5
        assert mock_cls.call_args.kwargs.get("verify") is True
