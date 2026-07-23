"""Startup assembly: appliance mode wires per-node heartbeat.

Verifies that ``run_warmup`` in appliance mode assembles the per-node
heartbeat module and wires the bidirectional instance <-> heartbeat
dependency:
- NodeHeartbeatStore + HeartbeatManager created and injected via deps
- recover_from_persisted called with distinct nodes from instance table
- NodeHeartbeatSweeper started (stashed on warmup_state)
- instance.set_heartbeat_service(manager) called (status derivation)
- generic mode does NOT assemble per-node heartbeat
- node heartbeat REST endpoint returns 200 in appliance, 404 in generic
"""

from __future__ import annotations

import importlib.util

import pytest


def _reload_backend(monkeypatch, tmp_path):
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))
    for n in list(__import__("sys").modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(__import__("sys").modules, n, raising=False)
    from a2x_registry.backend import startup
    from a2x_registry.backend.app import app
    return startup, app


def _shadow_heavy(monkeypatch):
    real_find_spec = importlib.util.find_spec
    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: None if n in heavy else real_find_spec(n, *a, **kw),
    )


def _run_warmup_synchronous(startup):
    startup.warmup_state.update({
        "ready": False, "stage": "starting", "progress": 0, "error": None,
    })
    startup.run_warmup()


# ── appliance mode assembles per-node heartbeat ────────────────

def test_appliance_mode_assembles_node_heartbeat_manager(monkeypatch, tmp_path):
    _shadow_heavy(monkeypatch)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    import a2x_registry.heartbeat.deps as hb_deps
    mgr = hb_deps.get_node_heartbeat_manager()
    assert mgr is not None, "appliance mode must assemble node heartbeat manager"
    from a2x_registry.heartbeat.service import HeartbeatManager
    assert isinstance(mgr, HeartbeatManager)


def test_appliance_mode_starts_node_sweeper(monkeypatch, tmp_path):
    _shadow_heavy(monkeypatch)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    sweeper = startup.warmup_state.get("_node_heartbeat_sweeper")
    assert sweeper is not None, "warmup_state must stash _node_heartbeat_sweeper"


def test_appliance_mode_wires_instance_to_heartbeat(monkeypatch, tmp_path):
    """instance.set_heartbeat_service(manager) called so _derive_status works."""
    _shadow_heavy(monkeypatch)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    import a2x_registry.instance.deps as inst_deps
    import a2x_registry.heartbeat.deps as hb_deps
    inst_svc = inst_deps.get_instance_service()
    hb_mgr = hb_deps.get_node_heartbeat_manager()
    assert inst_svc is not None and hb_mgr is not None
    # The instance service's status check callback must be wired to hb_mgr.is_expired
    assert inst_svc._is_node_expired is not None


def test_appliance_mode_recovers_from_persisted_nodes(monkeypatch, tmp_path):
    """recover_from_persisted called with distinct_nodes() at startup.

    With no instances, the recovery list is empty but must not error.
    """
    _shadow_heavy(monkeypatch)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    import a2x_registry.heartbeat.deps as hb_deps
    mgr = hb_deps.get_node_heartbeat_manager()
    # No instances -> no leases recovered
    assert mgr.store.list_nodes() == []


# ── generic mode does NOT assemble per-node heartbeat ──────────

def test_generic_mode_does_not_assemble_node_heartbeat(monkeypatch, tmp_path):
    _shadow_heavy(monkeypatch)
    monkeypatch.delenv("A2X_REGISTRY_MODE", raising=False)
    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    import a2x_registry.heartbeat.deps as hb_deps
    assert hb_deps.get_node_heartbeat_manager() is None


# ── REST endpoint availability ─────────────────────────────────

def test_node_heartbeat_endpoint_available_in_appliance(monkeypatch, tmp_path):
    _shadow_heavy(monkeypatch)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, app = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    from fastapi.testclient import TestClient
    c = TestClient(app)
    r = c.post("/api/nodes/192.168.0.12/heartbeat")
    assert r.status_code == 200
    assert r.json()["node"] == "192.168.0.12"


def test_node_heartbeat_endpoint_404_in_generic(monkeypatch, tmp_path):
    _shadow_heavy(monkeypatch)
    monkeypatch.delenv("A2X_REGISTRY_MODE", raising=False)
    startup, app = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    from fastapi.testclient import TestClient
    c = TestClient(app)
    r = c.post("/api/nodes/192.168.0.12/heartbeat")
    assert r.status_code == 404


def test_lease_config_endpoint_available_in_appliance(monkeypatch, tmp_path):
    _shadow_heavy(monkeypatch)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, app = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    from fastapi.testclient import TestClient
    c = TestClient(app)
    r = c.get("/api/lease-config")
    assert r.status_code == 200
    body = r.json()
    assert body["enabled"] is True
