"""YuanrongSandboxClient 单元测试（TDD：先于实现编写）。

客户端从 jiuwenswarm 仓的 YuanrongFrontendAgentClient 复制裁剪而来，
只保留沙箱管理子集（create_sandbox / delete_sandbox / get_agent_info /
wait_until_running）。本文件锁定其对外契约：

- create：payload 组装 + runtime_spec 规范化校验 + 响应解析
- delete：404 / code=404 幂等视作成功
- get_agent_info：instance dict 解析 + 顶层 status 兜底
- wait_until_running：显式 running 才就绪；failed 状态报错；超时是超时异常
- HTTP 层：urllib 阻塞调用由 asyncio.to_thread 包裹（不阻塞事件循环）

全部经 monkeypatch urllib.request.urlopen 打桩，不发真实网络请求。
"""

from __future__ import annotations

import asyncio
import io
import json
import urllib.error

import pytest

from a2x_registry.instance.yuanrong_client import (
    SandboxInfo,
    YuanrongAgentApiError,
    YuanrongAgentTimeoutError,
    YuanrongSandboxClient,
)


# ── 打桩工具 ────────────────────────────────────────────────────────

class _FakeResp:
    """urlopen 返回的上下文管理器响应。"""

    def __init__(self, status: int, body: bytes):
        self.status = status
        self._body = body

    def read(self) -> bytes:
        return self._body

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def _ok(body: dict, status: int = 200) -> _FakeResp:
    return _FakeResp(status, json.dumps(body).encode("utf-8"))


@pytest.fixture
def client() -> YuanrongSandboxClient:
    return YuanrongSandboxClient(frontend_endpoint="http://127.0.0.1:18888")


def _install(monkeypatch, handler):
    """把 urlopen 替换为 handler(request) -> _FakeResp | raise。"""

    def fake_urlopen(req, timeout=None):
        return handler(req)

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)


RUNTIME_SPEC = {
    "runtime": "python3.11",
    "rootfs": {"imageurl": "harbor.local/adapted/opencode:v0.2.0", "user": "agentos"},
    "cpu": 1000,
    "memory": 2048,
}


# ── create_sandbox ──────────────────────────────────────────────────

def test_create_sandbox_ok(monkeypatch, client):
    captured = {}

    def handler(req):
        captured["url"] = req.full_url
        captured["method"] = req.get_method()
        captured["payload"] = json.loads(req.data.decode("utf-8"))
        return _ok({"code": 200, "instance_id": "sbx-1", "status": "running"})

    _install(monkeypatch, handler)
    info = asyncio.run(client.create_sandbox(
        namespace="default", name="user-01+opencode",
        workspace="/app", runtime_spec=RUNTIME_SPEC,
    ))
    assert isinstance(info, SandboxInfo)
    assert info.sandbox_id == "sbx-1"
    assert captured["url"] == "http://127.0.0.1:18888/api/agent"
    assert captured["method"] == "POST"
    assert captured["payload"]["namespace"] == "default"
    assert captured["payload"]["name"] == "user-01+opencode"
    assert captured["payload"]["workspace"] == "/app"
    spec = captured["payload"]["runtime_spec"]
    assert spec["runtime"] == "python3.11"
    assert spec["rootfs"]["imageurl"] == "harbor.local/adapted/opencode:v0.2.0"
    assert spec["cpu"] == 1000 and spec["memory"] == 2048


def test_create_sandbox_env_vars_and_mounts(monkeypatch, client):
    captured = {}

    def handler(req):
        captured["payload"] = json.loads(req.data.decode("utf-8"))
        return _ok({"code": 200, "instance_id": "sbx-2"})

    _install(monkeypatch, handler)
    asyncio.run(client.create_sandbox(
        namespace="default", name="u+f", workspace="/app",
        runtime_spec=RUNTIME_SPEC,
        env_vars={"A2X_LLM_KEY": "k"},
        mounts=[{"source": "/data/agent", "target": "/data", "readonly": False}],
    ))
    assert captured["payload"]["env_vars"] == {"A2X_LLM_KEY": "k"}
    assert captured["payload"]["mounts"][0]["target"] == "/data"


@pytest.mark.parametrize("spec", [
    None,
    "not-a-dict",
    {"rootfs": {"imageurl": "x"}},                      # 缺 runtime
    {"runtime": "python3.11"},                           # 缺 rootfs
    {"runtime": "python3.11", "rootfs": {}},             # 缺 imageurl
])
def test_create_sandbox_invalid_runtime_spec(client, spec):
    with pytest.raises(ValueError):
        asyncio.run(client.create_sandbox(
            namespace="default", name="u+f", workspace="/app", runtime_spec=spec,
        ))


@pytest.mark.parametrize("kwargs", [
    {"namespace": "", "name": "u+f", "workspace": "/app"},
    {"namespace": "default", "name": "", "workspace": "/app"},
    {"namespace": "default", "name": "u+f", "workspace": ""},
    {"namespace": "default", "name": "u+f", "workspace": "app"},  # 非绝对路径
])
def test_create_sandbox_invalid_args(client, kwargs):
    with pytest.raises(ValueError):
        asyncio.run(client.create_sandbox(runtime_spec=RUNTIME_SPEC, **kwargs))


def test_create_sandbox_legacy_port_probe(monkeypatch, client):
    """rootfs.ports 兜底生成 startup 探针（与源实现一致）。"""
    captured = {}

    def handler(req):
        captured["payload"] = json.loads(req.data.decode("utf-8"))
        return _ok({"code": 200, "instance_id": "sbx-3"})

    _install(monkeypatch, handler)
    spec = dict(RUNTIME_SPEC, rootfs={"imageurl": "x", "ports": ["tcp:2222"]})
    asyncio.run(client.create_sandbox(
        namespace="default", name="u+f", workspace="/app", runtime_spec=spec,
    ))
    probes = captured["payload"]["runtime_spec"]["probes"]
    assert probes["startup"]["tcpSocket"]["port"] == 2222


def test_create_sandbox_missing_instance_id(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok({"code": 200}))
    with pytest.raises(YuanrongAgentApiError, match="instance_id"):
        asyncio.run(client.create_sandbox(
            namespace="default", name="u+f", workspace="/app", runtime_spec=RUNTIME_SPEC,
        ))


def test_create_sandbox_http_error(monkeypatch, client):
    def handler(req):
        raise urllib.error.HTTPError(
            req.full_url, 500, "boom", {}, io.BytesIO(b'{"code":500,"message":"down"}'),
        )

    _install(monkeypatch, handler)
    with pytest.raises(YuanrongAgentApiError, match="agent API failed"):
        asyncio.run(client.create_sandbox(
            namespace="default", name="u+f", workspace="/app", runtime_spec=RUNTIME_SPEC,
        ))


def test_create_sandbox_timeout(monkeypatch, client):
    def handler(req):
        raise TimeoutError("timed out")

    _install(monkeypatch, handler)
    with pytest.raises(YuanrongAgentTimeoutError):
        asyncio.run(client.create_sandbox(
            namespace="default", name="u+f", workspace="/app", runtime_spec=RUNTIME_SPEC,
        ))


# ── delete_sandbox ──────────────────────────────────────────────────

def test_delete_sandbox_ok(monkeypatch, client):
    captured = {}

    def handler(req):
        captured["url"] = req.full_url
        captured["method"] = req.get_method()
        return _ok({"code": 200})

    _install(monkeypatch, handler)
    asyncio.run(client.delete_sandbox("sbx-1"))
    assert captured["method"] == "DELETE"
    assert captured["url"].endswith("/api/agent/sbx-1")


def test_delete_sandbox_http_404_is_success(monkeypatch, client):
    def handler(req):
        raise urllib.error.HTTPError(req.full_url, 404, "gone", {}, io.BytesIO(b"{}"))

    _install(monkeypatch, handler)
    asyncio.run(client.delete_sandbox("sbx-1"))  # 不抛即通过


def test_delete_sandbox_code_404_is_success(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok({"code": 404}, status=200))
    asyncio.run(client.delete_sandbox("sbx-1"))


def test_delete_sandbox_failed(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok({"code": 500}, status=500))
    with pytest.raises(YuanrongAgentApiError):
        asyncio.run(client.delete_sandbox("sbx-1"))


def test_delete_sandbox_empty_id(client):
    with pytest.raises(ValueError):
        asyncio.run(client.delete_sandbox(" "))


def test_delete_sandbox_timeout(monkeypatch, client):
    def handler(req):
        raise TimeoutError("timed out")

    _install(monkeypatch, handler)
    with pytest.raises(YuanrongAgentTimeoutError):
        asyncio.run(client.delete_sandbox("sbx-1"))


# ── get_agent_info ──────────────────────────────────────────────────

def test_get_agent_info(monkeypatch, client):
    captured = {}

    def handler(req):
        captured["url"] = req.full_url
        captured["method"] = req.get_method()
        return _ok({"code": 200, "instance": {
            "instance_id": "sbx-1", "status": "running",
            "node_ip": "192.168.0.12", "sandbox_ip": "10.244.1.7",
        }})

    _install(monkeypatch, handler)
    info = asyncio.run(client.get_agent_info("sbx-1"))
    assert captured["method"] == "GET"
    assert captured["url"].endswith("/api/agent/sbx-1")
    assert info["node_ip"] == "192.168.0.12"
    assert info["sandbox_ip"] == "10.244.1.7"
    assert info["status"] == "running"


def test_get_agent_info_top_status_fallback(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok(
        {"code": 200, "status": "running", "instance": {"instance_id": "sbx-1"}},
    ))
    info = asyncio.run(client.get_agent_info("sbx-1"))
    assert info["status"] == "running"


def test_get_agent_info_missing_instance(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok({"code": 200}))
    assert asyncio.run(client.get_agent_info("sbx-1")) == {}


# ── wait_until_running ──────────────────────────────────────────────

def test_wait_until_running_immediate(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok({"code": 200, "instance": {
        "instance_id": "sbx-1", "status": "running",
    }}))
    info = asyncio.run(client.wait_until_running("sbx-1"))
    assert info["status"] == "running"


def test_wait_until_running_failed_status(monkeypatch, client):
    _install(monkeypatch, lambda req: _ok({"code": 200, "instance": {
        "instance_id": "sbx-1", "status": "failed",
    }}))
    with pytest.raises(YuanrongAgentApiError, match="failed"):
        asyncio.run(client.wait_until_running("sbx-1"))


def test_wait_until_running_timeout_is_timeout_error(monkeypatch):
    """等待超时必须是超时异常（调用方据此回 504），与元戎失败区分。"""
    c = YuanrongSandboxClient(
        frontend_endpoint="http://127.0.0.1:18888",
        wait_running_timeout_s=0.05,
        wait_running_interval_s=0.01,
    )
    _install(monkeypatch, lambda req: _ok({"code": 200, "instance": {
        "instance_id": "sbx-1", "status": "starting",
    }}))
    with pytest.raises(YuanrongAgentTimeoutError):
        asyncio.run(c.wait_until_running("sbx-1"))


def test_wait_until_running_missing_status_not_ready(monkeypatch, client):
    """缺 status 的部分响应不算就绪，持续轮询直到超时。"""
    c = YuanrongSandboxClient(
        frontend_endpoint="http://127.0.0.1:18888",
        wait_running_timeout_s=0.05,
        wait_running_interval_s=0.01,
    )
    _install(monkeypatch, lambda req: _ok({"code": 200, "instance": {"instance_id": "sbx-1"}}))
    with pytest.raises(YuanrongAgentTimeoutError):
        asyncio.run(c.wait_until_running("sbx-1"))


def test_wait_until_running_polls_until_running(monkeypatch):
    c = YuanrongSandboxClient(
        frontend_endpoint="http://127.0.0.1:18888",
        wait_running_timeout_s=5.0,
        wait_running_interval_s=0.01,
    )
    states = iter(["starting", "starting", "running"])

    def handler(req):
        return _ok({"code": 200, "instance": {
            "instance_id": "sbx-1", "status": next(states),
        }})

    _install(monkeypatch, handler)
    info = asyncio.run(c.wait_until_running("sbx-1"))
    assert info["status"] == "running"


# ── 构造与属性 ──────────────────────────────────────────────────────

def test_constructor_defaults():
    c = YuanrongSandboxClient(frontend_endpoint="http://x:1")
    assert c.agent_namespace == "default"
    assert c.frontend_endpoint == "http://x:1"


def test_empty_endpoint_rejected():
    with pytest.raises(ValueError):
        YuanrongSandboxClient(frontend_endpoint="  ")


def test_blocking_urlopen_runs_in_thread(monkeypatch, client):
    """元戎阻塞 HTTP 必须包在 to_thread：事件循环线程不执行 urlopen。"""
    import threading
    loop_thread = threading.current_thread()

    def handler(req):
        assert threading.current_thread() is not loop_thread
        return _ok({"code": 200, "instance_id": "sbx-thread"})

    _install(monkeypatch, handler)
    info = asyncio.run(client.create_sandbox(
        namespace="default", name="u+f", workspace="/app", runtime_spec=RUNTIME_SPEC,
    ))
    assert info.sandbox_id == "sbx-thread"
