"""实例管理 router 测试（memory 后端，TestClient）。

覆盖端点 + HTTP 状态码映射：
- POST /api/instances → 200 注册 / 400 校验失败
- PATCH /api/instances/{sid} → 200 更新 / 404 不存在
- DELETE /api/instances/{sid} → 200 {deleted: true/false}
- GET /api/instances → 200 列表 + 过滤 + include_unhealthy
- 未装配实例模块 → 404
"""

from __future__ import annotations

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from a2x_registry.instance.deps import set_instance_service
from a2x_registry.instance.router import router as instance_router

from .conftest import make_entry


def _make_app() -> FastAPI:
    """构建只挂 instance router 的最小 app。"""
    app = FastAPI()
    app.include_router(instance_router)
    return app


@pytest.fixture
def client(instance_svc):
    """TestClient，instance_svc fixture 已注入全局 deps。"""
    app = _make_app()
    return TestClient(app)


def _register(client, entry=None):
    if entry is None:
        entry = make_entry()
    return client.post("/api/instances", json=entry)


# ── POST /api/instances ────────────────────────────────────────

def test_post_register_success(client):
    r = _register(client)
    assert r.status_code == 200
    body = r.json()
    assert body["service_id"] == make_entry()["service_id"]
    assert body["kind"] == "三方"
    assert body["status"] == "运行"
    assert body["created_at"]
    assert body["last_active_at"]


def test_post_invalid_kind_400(client):
    entry = make_entry(kind="xxx")
    r = client.post("/api/instances", json=entry)
    assert r.status_code == 400


def test_post_missing_field_400(client):
    entry = make_entry()
    del entry["node"]
    r = client.post("/api/instances", json=entry)
    # pydantic 先拦（422）或 service 拦（400），都算「校验失败」
    assert r.status_code in (400, 422)


def test_post_not_assembled_404():
    """未装配实例模块 → 404。"""
    set_instance_service(None)
    app = _make_app()
    c = TestClient(app)
    r = c.post("/api/instances", json=make_entry())
    assert r.status_code == 404


# ── PATCH /api/instances/{sid} ─────────────────────────────────

def test_patch_update_success(client):
    _register(client)
    sid = make_entry()["service_id"]
    r = client.patch(f"/api/instances/{sid}", json={"node": "10.0.0.9", "address": "10.9.9.9:8080"})
    assert r.status_code == 200
    body = r.json()
    assert body["node"] == "10.0.0.9"
    assert body["address"] == "10.9.9.9:8080"


def test_patch_not_found_404(client):
    r = client.patch("/api/instances/generic_nope", json={"node": "10.0.0.1"})
    assert r.status_code == 404


# ── DELETE /api/instances/{sid} ────────────────────────────────

def test_delete_existing(client):
    _register(client)
    sid = make_entry()["service_id"]
    r = client.delete(f"/api/instances/{sid}")
    assert r.status_code == 200
    assert r.json() == {"service_id": sid, "deleted": True}


def test_delete_missing_idempotent(client):
    r = client.delete("/api/instances/generic_nope")
    assert r.status_code == 200
    assert r.json() == {"service_id": "generic_nope", "deleted": False}


# ── GET /api/instances ─────────────────────────────────────────

def test_get_list_all(client):
    _register(client, make_entry(user="alice", framework="langchain", node="192.168.0.11"))
    _register(client, make_entry(user="bob", framework="langchain", node="192.168.0.12"))
    r = client.get("/api/instances")
    assert r.status_code == 200
    assert len(r.json()) == 2


def test_get_list_filter_by_node(client):
    _register(client, make_entry(user="alice", framework="langchain", node="192.168.0.11"))
    _register(client, make_entry(user="bob", framework="langchain", node="192.168.0.12"))
    r = client.get("/api/instances", params={"node": "192.168.0.11"})
    assert r.status_code == 200
    rows = r.json()
    assert len(rows) == 1
    assert rows[0]["node"] == "192.168.0.11"


def test_get_list_filter_by_user(client):
    _register(client, make_entry(user="alice", framework="langchain", node="192.168.0.11"))
    _register(client, make_entry(user="bob", framework="langchain", node="192.168.0.12"))
    r = client.get("/api/instances", params={"user": "alice"})
    assert r.status_code == 200
    rows = r.json()
    assert len(rows) == 1
    assert rows[0]["user"] == "alice"


def test_get_list_include_unhealthy(client, instance_svc):
    """默认排除异常；?include_unhealthy=true 返回全部。"""
    _register(client, make_entry(user="alice", framework="langchain", node="192.168.0.11"))
    _register(client, make_entry(user="bob", framework="langchain", node="192.168.0.12"))
    # 标记 192.168.0.11 异常
    instance_svc.set_heartbeat_check(lambda node: node == "192.168.0.11")

    # 默认 → 只剩 192.168.0.12
    r = client.get("/api/instances")
    assert len(r.json()) == 1
    assert r.json()[0]["node"] == "192.168.0.12"

    # include_unhealthy=true → 全部
    r = client.get("/api/instances", params={"include_unhealthy": "true"})
    assert len(r.json()) == 2
