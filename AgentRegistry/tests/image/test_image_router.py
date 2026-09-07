"""镜像管理 router 测试（memory 后端，TestClient，name 主键模型）。

仅覆盖 HTTP 专属关注点：端点路由 + 状态码映射 + 分页 header +
framework_version 过渡期回退 + 未装配 404。业务逻辑（扁平列表、
过滤、排序、默认版本、launch-spec 透传、deregister 在用校验）由
test_image_service.py 覆盖，此处不重复。
"""

from __future__ import annotations

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from a2x_registry.image.deps import set_image_service
from a2x_registry.image.router import router as image_router

from .conftest import make_runtime_spec, make_register_body, make_access_mode


def _make_app() -> FastAPI:
    app = FastAPI()
    app.include_router(image_router)
    return app


@pytest.fixture
def client(image_svc):
    app = _make_app()
    return TestClient(app)


def _register(client, name="opencode", ver="v0.2.0", framework="opencode", **kw):
    body = make_register_body(name=name, version=ver, framework=framework, **kw)
    return client.post("/api/images", json=body)


# ── POST /api/images ────────────────────────────────────────────

def test_post_register_first_default(client):
    r = _register(client)
    assert r.status_code == 200
    body = r.json()
    assert body == {
        "name": "opencode",
        "framework": "opencode",
        "version": "v0.2.0",
        "status": "registered",
    }


def test_post_missing_version_rejected(client):
    """version 与 framework_version 都缺 -> 400。"""
    body = make_register_body()
    del body["version"]
    r = client.post("/api/images", json=body)
    assert r.status_code == 400


def test_post_framework_version_transition_fallback(client):
    """过渡期兼容：只给 deprecated framework_version 也能注册为该 version。"""
    body = make_register_body()
    body["framework_version"] = body.pop("version")
    r = client.post("/api/images", json=body)
    assert r.status_code == 200
    assert r.json()["version"] == "v0.2.0"


# ── GET /api/images (flat) ──────────────────────────────────────

def test_get_list_flat(client):
    _register(client, ver="v0.2.0")
    _register(client, ver="v0.1.0")
    r = client.get("/api/images")
    assert r.status_code == 200
    rows = r.json()
    assert len(rows) == 2
    assert isinstance(rows, list)
    assert rows[0]["name"] == "opencode"
    assert rows[0]["version"] == "v0.2.0"  # version_key DESC，新版本在前


def test_get_list_pagination_headers(client):
    for ver in ["v0.3.0", "v0.2.0", "v0.1.0"]:
        _register(client, ver=ver)
    r = client.get("/api/images", params={"size": 2, "page": 1})
    assert r.status_code == 200
    assert r.headers["X-Total-Count"] == "3"
    assert r.headers["X-Page"] == "1"
    assert r.headers["X-Total-Pages"] == "2"
    assert r.headers["X-Page-Size"] == "2"
    assert len(r.json()) == 2


# ── GET /api/images/{name}/launch-spec ──────────────────────────

def test_get_launch_spec_with_version(client):
    _register(client, runtime_spec=make_runtime_spec(cpu=1500))
    r = client.get("/api/images/opencode/launch-spec", params={"version": "v0.2.0"})
    assert r.status_code == 200
    body = r.json()
    assert body["name"] == "opencode"
    assert body["version"] == "v0.2.0"
    assert body["runtime_spec"]["cpu"] == 1500
    assert body["runtime_spec"]["rootfs"]["imageurl"] == "harbor.local/adapted/opencode:v0.2.0"
    assert body["env_vars"] == {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    assert body["workspace"] == "/app"
    assert body["access_mode"] == make_access_mode()
    assert "imageurl" not in body
    assert "cpu" not in body


def test_get_launch_spec_not_found(client):
    r = client.get("/api/images/nonexistent/launch-spec")
    assert r.status_code == 404


# ── PUT /api/images/{name}/default ──────────────────────────────

def test_put_set_default(client):
    _register(client, ver="v0.2.0")
    _register(client, ver="v0.1.0")
    r = client.put("/api/images/opencode/default", json={"version": "v0.1.0"})
    assert r.status_code == 200
    assert r.json()["default"] == "v0.1.0"


def test_put_set_default_framework_version_fallback(client):
    """过渡期兼容：body 只给 framework_version 也能设默认。"""
    _register(client, ver="v0.2.0")
    _register(client, ver="v0.1.0")
    r = client.put(
        "/api/images/opencode/default", json={"framework_version": "v0.1.0"}
    )
    assert r.status_code == 200
    assert r.json()["default"] == "v0.1.0"


def test_put_set_default_empty_version_rejected(client):
    _register(client, ver="v0.2.0")
    r = client.put("/api/images/opencode/default", json={})
    assert r.status_code == 400


def test_put_set_default_not_found(client):
    _register(client, ver="v0.2.0")
    r = client.put("/api/images/opencode/default", json={"version": "v9.9.9"})
    assert r.status_code == 404


# ── DELETE /api/images/{name}/{version} ─────────────────────────

def test_delete_deregister(client):
    _register(client)
    r = client.delete("/api/images/opencode/v0.2.0")
    assert r.status_code == 200
    assert r.json() == {
        "name": "opencode",
        "framework": "opencode",
        "version": "v0.2.0",
        "status": "deregistered",
    }


def test_delete_in_use_409(client, image_svc):
    _register(client, framework="opencode")
    image_svc._table_svc.register("instances", {
        "service_id": "generic_abc123",
        "kind": "三方",
        "framework": "opencode",
        "framework_version": "v0.2.0",
        "node": "node-1",
        "user": "user-01",
        "data": {},
    })
    r = client.delete("/api/images/opencode/v0.2.0")
    assert r.status_code == 409


def test_delete_not_found(client):
    r = client.delete("/api/images/nonexistent/v0.0.0")
    assert r.status_code == 404


# ── 未装配镜像模块 -> 404 ─────────────────────────────────────────

def test_routes_404_when_not_assembled():
    set_image_service(None)
    app = _make_app()
    c = TestClient(app)
    assert c.get("/api/images").status_code == 404
    body = make_register_body()
    assert c.post("/api/images", json=body).status_code == 404
