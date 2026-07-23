"""Node heartbeat router tests (appliance mode, TestClient).

Covers REST contract:
- POST /api/nodes/{node}/heartbeat -> 200 {node, state, ttl_seconds, expires_at}
- POST /api/nodes/{node}/heartbeat with empty body -> 200 (body optional)
- GET /api/lease-config -> 200 {enabled, min_ttl, max_ttl, grace_period}
- POST /api/lease-config -> 200 updated config
- Module not assembled -> 404 for all node heartbeat routes
- disabled config -> POST heartbeat returns 400
"""

from __future__ import annotations

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from a2x_registry.heartbeat.deps import set_node_heartbeat_manager
from a2x_registry.heartbeat.router import node_router
from a2x_registry.heartbeat.service import HeartbeatManager
from a2x_registry.heartbeat.store import NodeHeartbeatStore


def _make_app() -> FastAPI:
    app = FastAPI()
    app.include_router(node_router)
    return app


@pytest.fixture
def manager():
    mgr = HeartbeatManager(NodeHeartbeatStore())
    set_node_heartbeat_manager(mgr)
    yield mgr
    set_node_heartbeat_manager(None)


@pytest.fixture
def client(manager):
    return TestClient(_make_app())


# ── POST /api/nodes/{node}/heartbeat ───────────────────────────

def test_post_node_heartbeat_success(client):
    r = client.post("/api/nodes/192.168.0.12/heartbeat")
    assert r.status_code == 200
    body = r.json()
    assert body["node"] == "192.168.0.12"
    assert body["state"] == "healthy"
    assert body["ttl_seconds"] > 0
    assert body["expires_at"] > 0


def test_post_node_heartbeat_with_status_body(client):
    """Optional status field is accepted (piggyback, best-effort)."""
    r = client.post(
        "/api/nodes/10.0.0.1/heartbeat",
        json={"status": "busy"},
    )
    assert r.status_code == 200
    assert r.json()["node"] == "10.0.0.1"


def test_post_node_heartbeat_renew(client, manager):
    """Second heartbeat renews the lease (soft recovery)."""
    client.post("/api/nodes/10.0.0.1/heartbeat")
    # Mark unhealthy, then heartbeat -> healthy
    lease = manager.store.get_lease("10.0.0.1")
    lease.state = __import__("a2x_registry.common.lease", fromlist=["LeaseState"]).LeaseState.UNHEALTHY
    r = client.post("/api/nodes/10.0.0.1/heartbeat")
    assert r.status_code == 200
    assert r.json()["state"] == "healthy"


def test_post_node_heartbeat_not_assembled_404():
    """Manager not injected -> 404."""
    set_node_heartbeat_manager(None)
    app = _make_app()
    c = TestClient(app)
    r = c.post("/api/nodes/10.0.0.1/heartbeat")
    assert r.status_code == 404


def test_post_node_heartbeat_disabled_400(client, manager):
    manager.store.update_config(enabled=False)
    r = client.post("/api/nodes/10.0.0.1/heartbeat")
    assert r.status_code == 400


# ── GET /api/lease-config ──────────────────────────────────────

def test_get_lease_config_defaults(client):
    r = client.get("/api/lease-config")
    assert r.status_code == 200
    body = r.json()
    assert body["enabled"] is True
    assert "min_ttl" in body
    assert "max_ttl" in body
    assert "grace_period" in body


def test_get_lease_config_not_assembled_404():
    set_node_heartbeat_manager(None)
    app = _make_app()
    c = TestClient(app)
    r = c.get("/api/lease-config")
    assert r.status_code == 404


# ── POST /api/lease-config ─────────────────────────────────────

def test_post_lease_config_update(client, manager):
    r = client.post("/api/lease-config", json={
        "enabled": True, "min_ttl": 120, "max_ttl": 7200, "grace_period": 45,
    })
    assert r.status_code == 200
    body = r.json()
    assert body["min_ttl"] == 120
    assert body["grace_period"] == 45
    # Config actually mutated on the store
    assert manager.store.get_config().min_ttl == 120


def test_post_lease_config_not_assembled_404():
    set_node_heartbeat_manager(None)
    app = _make_app()
    c = TestClient(app)
    r = c.post("/api/lease-config", json={
        "enabled": True, "min_ttl": 10, "max_ttl": 3600, "grace_period": 30,
    })
    assert r.status_code == 404
