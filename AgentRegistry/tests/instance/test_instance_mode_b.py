"""实例双模式（模式 B：registry 直连元戎）测试（TDD：先于实现编写）。

覆盖：
- POST /api/instances 形状判别（模式 A/B/400）+ 模式 B 全流程（幂等 / 重建 /
  等待 running / 错误映射 501/502/504/409）
- DELETE ?with_runtime=true（单个 / 幂等 / instance_id 空 / 元戎失败条目保留）
- DELETE ALL 批量（并发限流 / 逐条 results / 部分失败不回滚 / 在途条目记 error）
- in-flight 锁互斥（创建 vs 删除）、条目写入重试

元戎客户端用 Fake 打桩（不打 HTTP）；客户端本身的行为由
test_yuanrong_client.py 锁定。
"""

from __future__ import annotations

import asyncio

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from a2x_registry.instance import constants
from a2x_registry.instance.deps import set_instance_service
from a2x_registry.instance.errors import (
    InstanceInProgressError,
    InstanceStoreError,
    RuntimeNotConfiguredError,
)
from a2x_registry.instance.router import router as instance_router
from a2x_registry.instance.service import InstanceService
from a2x_registry.instance.yuanrong_client import (
    SandboxInfo,
    YuanrongAgentApiError,
    YuanrongAgentTimeoutError,
)

from .conftest import make_entry


# ── Fake 元戎客户端 ─────────────────────────────────────────────────

class FakeYuanrongClient:
    """打桩元戎客户端：与真实客户端同款方法面 + 失败/延时注入。"""

    agent_namespace = "default"
    wait_running_timeout_s = 60.0

    def __init__(self):
        self.create_calls: list[dict] = []
        self.delete_calls: list[str] = []
        self.fail_create = False
        self.timeout_create = False
        self.fail_delete = False
        self.timeout_delete = False
        self.fail_wait = False
        self.fail_delete_ids: set[str] = set()
        self.create_delay_s = 0.0
        self.wait_called = 0
        self._counter = 0

    async def create_sandbox(self, *, namespace, name, workspace,
                             runtime_spec, env_vars=None, mounts=None):
        if not isinstance(runtime_spec, dict) or not runtime_spec.get("runtime") \
                or not (runtime_spec.get("rootfs") or {}).get("imageurl"):
            raise ValueError("runtime_spec.runtime/rootfs.imageurl required")
        self._counter += 1
        if self.create_delay_s:
            await asyncio.sleep(self.create_delay_s)
        if self.fail_create:
            raise YuanrongAgentApiError("agent API failed: code=500")
        if self.timeout_create:
            raise YuanrongAgentTimeoutError("request timeout after 300s")
        self.create_calls.append({
            "namespace": namespace, "name": name, "workspace": workspace,
            "runtime_spec": runtime_spec, "env_vars": env_vars, "mounts": mounts,
        })
        return SandboxInfo(sandbox_id=f"sbx-{self._counter}")

    async def delete_sandbox(self, sandbox_id):
        self.delete_calls.append(sandbox_id)
        if sandbox_id in self.fail_delete_ids or self.fail_delete:
            raise YuanrongAgentApiError("agent API failed: code=500")
        if self.timeout_delete:
            raise YuanrongAgentTimeoutError("request timeout after 300s")

    async def get_agent_info(self, instance_id):
        return {
            "instance_id": instance_id, "status": "running",
            "node_ip": "192.168.0.12", "sandbox_ip": "10.244.1.7",
        }

    async def wait_until_running(self, instance_id):
        self.wait_called += 1
        if self.fail_wait:
            raise YuanrongAgentTimeoutError("agent instance not running after 60s")
        return {
            "instance_id": instance_id, "status": "running",
            "node_ip": "192.168.0.12", "sandbox_ip": "10.244.1.7",
        }


# ── fixtures ────────────────────────────────────────────────────────

@pytest.fixture
def fake_yuanrong():
    return FakeYuanrongClient()


@pytest.fixture
def svc(table_svc, fake_yuanrong):
    """装配了 Fake 元戎客户端的 InstanceService。"""
    s = InstanceService(table_svc, yuanrong_client=fake_yuanrong)
    set_instance_service(s)
    yield s
    set_instance_service(None)


@pytest.fixture
def svc_no_runtime(table_svc):
    """未配置元戎（client=None）的 InstanceService。"""
    s = InstanceService(table_svc)
    set_instance_service(s)
    yield s
    set_instance_service(None)


def _make_app() -> FastAPI:
    app = FastAPI()
    app.include_router(instance_router)
    return app


@pytest.fixture
def client(svc):
    return TestClient(_make_app())


@pytest.fixture
def client_no_rt(svc_no_runtime):
    return TestClient(_make_app())


def mode_b_payload(name="user-01+opencode", version="v0.2.0",
                   image_name="opencode", **over):
    p = {
        "name": name, "workspace": "/app", "version": version,
        "runtime_spec": {
            "runtime": "python3.11",
            "rootfs": {"imageurl": "harbor.local/adapted/opencode:v0.2.0"},
        },
    }
    if image_name is not None:
        p["image_name"] = image_name
    p.update(over)
    return p


# ══ POST /api/instances —— 形状判别 ═════════════════════════════════

def test_mode_b_create_success(client, fake_yuanrong):
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["service_id"] == "user-01+opencode"
    assert body["kind"] == "三方"
    assert body["framework"] == "opencode"
    assert body["framework_version"] == "v0.2.0"
    assert body["image_name"] == "opencode"
    assert body["node"] == "192.168.0.12"
    assert body["address"] == "10.244.1.7"      # 无端口 → sandbox_ip
    assert body["instance_id"] == "sbx-1"
    assert body["status"] == "运行"
    # namespace 以 registry 配置为准
    assert fake_yuanrong.create_calls[0]["namespace"] == "default"
    assert fake_yuanrong.create_calls[0]["name"] == "user-01+opencode"
    assert fake_yuanrong.wait_called == 1


def test_mode_b_create_with_port(client, fake_yuanrong):
    """元戎若返回 port，则 address 组装为 sandbox_ip:port。"""
    async def info_with_port(instance_id):
        return {"instance_id": instance_id, "status": "running",
                "node_ip": "1.1.1.1", "sandbox_ip": "10.0.0.1", "port": 4096}

    fake_yuanrong.get_agent_info = info_with_port
    fake_yuanrong.wait_until_running = info_with_port
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 200
    assert r.json()["address"] == "10.0.0.1:4096"


def test_mode_b_kind_jiuwenswarm(client):
    r = client.post("/api/instances", json=mode_b_payload(name="user-02+jiuwenswarm"))
    assert r.status_code == 200
    assert r.json()["kind"] == "九问"


def test_mode_b_idempotent_when_running(client, fake_yuanrong):
    r1 = client.post("/api/instances", json=mode_b_payload())
    r2 = client.post("/api/instances", json=mode_b_payload())
    assert r1.status_code == r2.status_code == 200
    assert r1.json()["instance_id"] == r2.json()["instance_id"]
    assert len(fake_yuanrong.create_calls) == 1   # 不二次拉起


def test_mode_b_recreate_when_not_running(client, fake_yuanrong):
    sid = "user-01+opencode"
    client.post("/api/instances", json=mode_b_payload())
    client.patch(f"/api/instances/{sid}", json={"status": "停止"})
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 200
    assert len(fake_yuanrong.create_calls) == 2   # 重新拉起
    assert r.json()["instance_id"] == "sbx-2"
    assert r.json()["status"] == "运行"


def test_mode_b_wait_disabled_uses_get_info(client, fake_yuanrong):
    fake_yuanrong.wait_running_timeout_s = 0.0
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 200
    assert fake_yuanrong.wait_called == 0
    assert r.json()["node"] == "192.168.0.12"


# ══ POST —— 400 校验 ═══════════════════════════════════════════════

def test_mode_b_name_without_plus_400(client):
    r = client.post("/api/instances", json=mode_b_payload(name="no-plus-user"))
    assert r.status_code == 400


def test_mode_b_name_empty_user_side_400(client):
    r = client.post("/api/instances", json=mode_b_payload(name="+opencode"))
    assert r.status_code == 400


def test_mode_b_name_empty_framework_side_400(client):
    r = client.post("/api/instances", json=mode_b_payload(name="user-01+"))
    assert r.status_code == 400


def test_mode_b_missing_version_400(client):
    p = mode_b_payload()
    del p["version"]
    r = client.post("/api/instances", json=p)
    assert r.status_code == 400


def test_mode_b_relative_workspace_400(client):
    r = client.post("/api/instances", json=mode_b_payload(workspace="app"))
    assert r.status_code == 400


def test_mode_b_invalid_runtime_spec_400(client):
    p = mode_b_payload(runtime_spec={"runtime": "python3.11"})
    r = client.post("/api/instances", json=p)
    assert r.status_code == 400


def test_yuanrong_fields_without_runtime_spec_400(client):
    p = mode_b_payload()
    del p["runtime_spec"]
    r = client.post("/api/instances", json=p)
    assert r.status_code == 400


def test_undeterminable_shape_400(client):
    r = client.post("/api/instances", json={"user": "user-01"})
    assert r.status_code == 400


def test_mode_b_wins_when_both_shapes(client, fake_yuanrong):
    p = mode_b_payload(node="9.9.9.9", address="9.9.9.9:1")
    r = client.post("/api/instances", json=p)
    assert r.status_code == 200
    # 落点由元戎返回覆盖，不用入参 node/address
    assert r.json()["node"] == "192.168.0.12"
    assert len(fake_yuanrong.create_calls) == 1


def test_mode_a_with_image_name(client):
    entry = make_entry(service_id="user-a+opencode")
    entry["image_name"] = "opencode"
    r = client.post("/api/instances", json=entry)
    assert r.status_code == 200
    assert r.json()["image_name"] == "opencode"
    # 查询可见
    rows = client.get("/api/instances", params={"user": entry["user"]}).json()
    assert rows[0]["image_name"] == "opencode"


# ══ POST —— 501 / 502 / 504 ════════════════════════════════════════

def test_mode_b_not_configured_501(client_no_rt):
    r = client_no_rt.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 501
    assert "runtime_not_configured" in r.text


def test_mode_b_yuanrong_failed_502(client, fake_yuanrong):
    fake_yuanrong.fail_create = True
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 502
    assert "yuanrong_failed" in r.text


def test_mode_b_yuanrong_timeout_504(client, fake_yuanrong):
    fake_yuanrong.timeout_create = True
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 504
    assert "yuanrong_timeout" in r.text


def test_mode_b_wait_timeout_504_no_entry(client, fake_yuanrong):
    fake_yuanrong.fail_wait = True
    r = client.post("/api/instances", json=mode_b_payload())
    assert r.status_code == 504
    # 条目未写入
    rows = client.get("/api/instances", params={"include_unhealthy": True}).json()
    assert rows == []


# ══ DELETE —— with_runtime ══════════════════════════════════════════

def test_delete_with_runtime_success(client, fake_yuanrong):
    client.post("/api/instances", json=mode_b_payload())
    r = client.delete("/api/instances/user-01+opencode?with_runtime=true")
    assert r.status_code == 200
    body = r.json()
    assert body == {"service_id": "user-01+opencode", "deleted": True,
                    "runtime_deleted": True}
    assert fake_yuanrong.delete_calls == ["sbx-1"]
    rows = client.get("/api/instances", params={"include_unhealthy": True}).json()
    assert rows == []


def test_delete_missing_entry_idempotent(client, fake_yuanrong):
    r = client.delete("/api/instances/ghost+opencode?with_runtime=true")
    assert r.status_code == 200
    assert r.json()["deleted"] is False
    assert r.json()["runtime_deleted"] is False
    assert fake_yuanrong.delete_calls == []


def test_delete_mode_a_entry_without_instance_id(client, fake_yuanrong):
    client.post("/api/instances", json=make_entry(service_id="user-a+claude"))
    r = client.delete("/api/instances/user-a+claude?with_runtime=true")
    assert r.status_code == 200
    assert r.json()["deleted"] is True
    assert r.json()["runtime_deleted"] is False
    assert fake_yuanrong.delete_calls == []


def test_delete_yuanrong_failed_502_keeps_entry(client, fake_yuanrong):
    client.post("/api/instances", json=mode_b_payload())
    fake_yuanrong.fail_delete = True
    r = client.delete("/api/instances/user-01+opencode?with_runtime=true")
    assert r.status_code == 502
    rows = client.get("/api/instances", params={"include_unhealthy": True}).json()
    assert len(rows) == 1


def test_delete_yuanrong_timeout_504_keeps_entry(client, fake_yuanrong):
    client.post("/api/instances", json=mode_b_payload())
    fake_yuanrong.timeout_delete = True
    r = client.delete("/api/instances/user-01+opencode?with_runtime=true")
    assert r.status_code == 504
    rows = client.get("/api/instances", params={"include_unhealthy": True}).json()
    assert len(rows) == 1


def test_delete_not_configured_501(client_no_rt):
    r = client_no_rt.delete("/api/instances/user-01+opencode?with_runtime=true")
    assert r.status_code == 501


def test_delete_without_with_runtime_skips_yuanrong(client, fake_yuanrong):
    client.post("/api/instances", json=mode_b_payload())
    r = client.delete("/api/instances/user-01+opencode")
    assert r.status_code == 200
    assert r.json()["deleted"] is True
    assert fake_yuanrong.delete_calls == []       # 不碰元戎


# ══ DELETE —— ALL 批量 ═════════════════════════════════════════════

def _seed_three(client):
    client.post("/api/instances", json=mode_b_payload(name="u1+opencode"))
    client.post("/api/instances", json=mode_b_payload(name="u2+aider", version="v1.0.0"))
    client.post("/api/instances", json=make_entry(service_id="u3+claude"))


def test_delete_all_with_runtime(client, fake_yuanrong):
    _seed_three(client)
    r = client.delete("/api/instances/ALL?with_runtime=true")
    assert r.status_code == 200
    body = r.json()
    assert body["total"] == 3
    assert body["deleted"] == 3
    assert {x["service_id"] for x in body["results"]} == {"u1+opencode", "u2+aider", "u3+claude"}
    # 模式 A 条目（u3 无 instance_id）不调元戎
    assert sorted(fake_yuanrong.delete_calls) == ["sbx-1", "sbx-2"]
    rows = client.get("/api/instances", params={"include_unhealthy": True}).json()
    assert rows == []


def test_delete_all_without_runtime(client, fake_yuanrong):
    _seed_three(client)
    r = client.delete("/api/instances/ALL")
    assert r.status_code == 200
    assert r.json()["deleted"] == 3
    assert fake_yuanrong.delete_calls == []


def test_delete_all_partial_failure_no_rollback(client, fake_yuanrong):
    _seed_three(client)
    fake_yuanrong.fail_delete_ids = {"sbx-1"}      # u1+opencode 的元戎实例
    r = client.delete("/api/instances/ALL?with_runtime=true")
    assert r.status_code == 200
    body = r.json()
    assert body["total"] == 3
    assert body["deleted"] == 2
    by_sid = {x["service_id"]: x for x in body["results"]}
    assert by_sid["u1+opencode"]["deleted"] is False
    assert "error" in by_sid["u1+opencode"]
    # 失败条目保留
    rows = client.get("/api/instances", params={"include_unhealthy": True}).json()
    assert [x["service_id"] for x in rows] == ["u1+opencode"]


# ══ 并发互斥 / 重试（service 级，单事件循环）════════════════════════

def test_create_inflight_conflict(table_svc, fake_yuanrong, monkeypatch):
    monkeypatch.setattr(constants, "LOCK_WAIT_S", 0.2)
    fake_yuanrong.create_delay_s = 0.3
    svc = InstanceService(table_svc, yuanrong_client=fake_yuanrong)
    payload = mode_b_payload()

    async def scenario():
        t1 = asyncio.create_task(svc.create_instance_runtime(payload))
        await asyncio.sleep(0.05)                  # t1 已持有锁
        with pytest.raises(InstanceInProgressError):
            await svc.create_instance_runtime(dict(payload))
        return await t1

    result = asyncio.run(scenario())
    assert result["instance_id"] == "sbx-1"
    assert not svc._inflight_locks["user-01+opencode"].locked()   # 已释放


def test_create_delete_mutual_exclusion(table_svc, fake_yuanrong, monkeypatch):
    monkeypatch.setattr(constants, "LOCK_WAIT_S", 0.2)
    fake_yuanrong.create_delay_s = 0.3
    svc = InstanceService(table_svc, yuanrong_client=fake_yuanrong)

    async def scenario():
        t1 = asyncio.create_task(svc.create_instance_runtime(mode_b_payload()))
        await asyncio.sleep(0.05)
        with pytest.raises(InstanceInProgressError):
            await svc.delete_instance_runtime("user-01+opencode")
        return await t1

    asyncio.run(scenario())


def test_delete_all_inflight_item_reports_error(table_svc):
    """ALL 批量遇到在途条目：逐条记 in_progress，其余照常删。"""
    fake = FakeYuanrongClient()
    svc = InstanceService(table_svc, yuanrong_client=fake)

    async def scenario():
        # 预置两条模式 A 条目
        svc.register_instance(make_entry(service_id="u1+aider"))
        svc.register_instance(make_entry(service_id="u2+claude"))
        # 持有 u1 的在途锁
        lock = svc._inflight_locks.setdefault("u1+aider", asyncio.Lock())
        await lock.acquire()
        try:
            return await svc.delete_all_instances(with_runtime=False)
        finally:
            lock.release()

    result = asyncio.run(scenario())
    assert result["total"] == 2
    assert result["deleted"] == 1
    by_sid = {x["service_id"]: x for x in result["results"]}
    assert by_sid["u1+aider"]["deleted"] is False
    assert by_sid["u1+aider"]["error"] == "in_progress"
    assert by_sid["u2+claude"]["deleted"] is True


def test_entry_write_retry_then_success(table_svc, fake_yuanrong, monkeypatch):
    """条目写入第一次失败、重试成功 → 正常返回。"""
    svc = InstanceService(table_svc, yuanrong_client=fake_yuanrong)
    original = table_svc.register
    calls = {"n": 0}

    def flaky(name, entry):
        calls["n"] += 1
        if calls["n"] == 1:
            raise RuntimeError("db transient failure")
        return original(name, entry)

    monkeypatch.setattr(table_svc, "register", flaky)
    result = asyncio.run(svc.create_instance_runtime(mode_b_payload()))
    assert result["instance_id"] == "sbx-1"
    assert calls["n"] == 2


def test_entry_write_retry_exhausted(table_svc, fake_yuanrong, monkeypatch):
    """条目写入重试耗尽 → InstanceStoreError（元戎实例留日志待对账）。"""
    svc = InstanceService(table_svc, yuanrong_client=fake_yuanrong)

    def always_fail(name, entry):
        raise RuntimeError("db down")

    monkeypatch.setattr(table_svc, "register", always_fail)
    with pytest.raises(InstanceStoreError):
        asyncio.run(svc.create_instance_runtime(mode_b_payload()))
    # 锁已释放（后续可重试）
    assert not svc._inflight_locks["user-01+opencode"].locked()
