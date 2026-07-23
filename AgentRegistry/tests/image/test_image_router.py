"""镜像管理 router 测试（memory 后端，TestClient，runtime_spec 透传模型）。

覆盖端点 + HTTP 状态码映射：
- POST /api/images -> 200 registered / updated
- GET /api/images -> 扁平列表 + 分页 header + uploaded_by 过滤 + runtime_spec 透传
- GET /api/images/{fw}/launch-spec -> 200 runtime_spec 透传 / 404
- PUT /api/images/{fw}/default -> 200 / 404
- DELETE /api/images/{fw}/{ver} -> 200 / 409 在用 / 404 不存在
- 未装配镜像模块 -> 404
"""

from __future__ import annotations

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from a2x_registry.image.deps import set_image_service
from a2x_registry.image.router import router as image_router

from .conftest import make_runtime_spec, make_register_body


def _make_app() -> FastAPI:
    app = FastAPI()
    app.include_router(image_router)
    return app


@pytest.fixture
def client(image_svc):
    app = _make_app()
    return TestClient(app)


def _register(client, fw="opencode", ver="v0.2.0", **kw):
    body = make_register_body(**kw)
    body["framework"] = fw
    body["framework_version"] = ver
    return client.post("/api/images", json=body)


# ── POST /api/images ────────────────────────────────────────────

def test_post_register_first_default(client):
    r = _register(client)
    assert r.status_code == 200
    body = r.json()
    assert body["framework"] == "opencode"
    assert body["framework_version"] == "v0.2.0"
    assert body["is_default"] is True
    assert body["status"] == "registered"


def test_post_reregister_updated(client):
    _register(client)
    r = _register(client, runtime_spec=make_runtime_spec(cpu=2000))
    assert r.status_code == 200
    assert r.json()["status"] == "updated"


# ── GET /api/images (flat) ──────────────────────────────────────

def test_get_list_flat(client):
    _register(client, ver="v0.2.0")
    _register(client, ver="v0.1.0")
    r = client.get("/api/images")
    assert r.status_code == 200
    rows = r.json()
    assert len(rows) == 2
    assert isinstance(rows, list)
    assert rows[0]["framework"] == "opencode"


def test_get_list_runtime_spec_passthrough(client):
    _register(client, runtime_spec=make_runtime_spec(cpu=1500))
    r = client.get("/api/images")
    assert r.status_code == 200
    row = r.json()[0]
    assert row["runtime_spec"]["cpu"] == 1500
    assert row["workspace"] == "/app"
    assert row["env_vars"] == {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    assert "imageurl" not in row
    assert "cpu" not in row


def test_get_list_filter_framework(client):
    _register(client, fw="opencode")
    _register(client, fw="ninequery", ver="v1.0.0")
    r = client.get("/api/images", params={"framework": "opencode"})
    assert r.status_code == 200
    rows = r.json()
    assert len(rows) == 1
    assert rows[0]["framework"] == "opencode"


def test_get_list_filter_uploaded_by(client):
    _register(client, fw="opencode", uploaded_by="alice")
    _register(client, fw="ninequery", ver="v1.0.0", uploaded_by="bob")
    r = client.get("/api/images", params={"uploaded_by": "alice"})
    assert r.status_code == 200
    rows = r.json()
    assert len(rows) == 1
    assert rows[0]["uploaded_by"] == "alice"


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


def test_get_list_pagination_page2(client):
    for ver in ["v0.3.0", "v0.2.0", "v0.1.0"]:
        _register(client, ver=ver)
    r = client.get("/api/images", params={"size": 2, "page": 2})
    assert r.status_code == 200
    assert r.headers["X-Total-Count"] == "3"
    assert len(r.json()) == 1


def test_get_list_empty(client):
    r = client.get("/api/images")
    assert r.status_code == 200
    assert r.json() == []


# ── GET /api/images/{fw}/launch-spec ────────────────────────────

def test_get_launch_spec_with_version(client):
    _register(client, runtime_spec=make_runtime_spec(cpu=1500))
    r = client.get("/api/images/opencode/launch-spec", params={"version": "v0.2.0"})
    assert r.status_code == 200
    body = r.json()
    assert body["framework"] == "opencode"
    assert body["framework_version"] == "v0.2.0"
    assert body["runtime_spec"]["cpu"] == 1500
    assert body["runtime_spec"]["rootfs"]["imageurl"] == "harbor.local/adapted/opencode:v0.2.0"
    assert body["env_vars"] == {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    assert body["workspace"] == "/app"
    assert "imageurl" not in body
    assert "cpu" not in body


def test_get_launch_spec_default_version(client):
    _register(client, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=1000))
    _register(client, ver="v0.1.0", runtime_spec=make_runtime_spec(cpu=500))
    r = client.get("/api/images/opencode/launch-spec")
    assert r.status_code == 200
    assert r.json()["framework_version"] == "v0.2.0"
    assert r.json()["runtime_spec"]["cpu"] == 1000


def test_get_launch_spec_not_found(client):
    r = client.get("/api/images/nonexistent/launch-spec")
    assert r.status_code == 404


# ── PUT /api/images/{fw}/default ────────────────────────────────

def test_put_set_default(client):
    _register(client, ver="v0.2.0")
    _register(client, ver="v0.1.0")
    r = client.put("/api/images/opencode/default", json={"framework_version": "v0.1.0"})
    assert r.status_code == 200
    assert r.json()["default"] == "v0.1.0"


def test_put_set_default_not_found(client):
    _register(client, ver="v0.2.0")
    r = client.put("/api/images/opencode/default", json={"framework_version": "v9.9.9"})
    assert r.status_code == 404


# ── DELETE /api/images/{fw}/{ver} ───────────────────────────────

def test_delete_deregister(client):
    _register(client)
    r = client.delete("/api/images/opencode/v0.2.0")
    assert r.status_code == 200
    assert r.json()["status"] == "deregistered"


def test_delete_in_use_409(client, image_svc):
    _register(client)
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
    body["framework"] = "x"
    body["framework_version"] = "v1"
    assert c.post("/api/images", json=body).status_code == 404
