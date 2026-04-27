"""Lite-mode end-to-end coverage of every endpoint the SDK actually hits.

Stubs out heavy extras and exercises the FastAPI app via TestClient. Each
SDK-facing path must respond 2xx; the heavy paths (search/build) must
respond 503 with a structured ``FeatureNotInstalledError`` body.

Run with::

    pytest tests/test_lite_e2e.py -v
"""

from __future__ import annotations

import importlib
import importlib.util
import os
import sys
import tempfile
import uuid
from pathlib import Path

import pytest


_HEAVY = ("numpy", "sentence_transformers", "chromadb", "tqdm")


@pytest.fixture(scope="module")
def lite_app(tmp_path_factory):
    """Boot the FastAPI app inside a lite-simulated env, fresh DB dir."""
    real_find_spec = importlib.util.find_spec
    importlib.util.find_spec = lambda n, *a, **kw: (
        None if n in _HEAVY else real_find_spec(n, *a, **kw)
    )
    tmp = tmp_path_factory.mktemp("a2x_home")
    os.environ["A2X_REGISTRY_HOME"] = str(tmp)

    # Force reload so the new env var takes effect on path resolution.
    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            del sys.modules[n]

    from a2x_registry.backend.app import app
    from a2x_registry.backend.startup import run_warmup

    run_warmup()

    from fastapi.testclient import TestClient
    yield TestClient(app)


def _agent_card(name: str = "agent-1") -> dict:
    return {
        "name": name,
        "description": "tester",
        "url": "http://example.invalid",
        "version": "1.0",
        "protocolVersion": "0.0.1",
        "capabilities": {},
        "defaultInputModes": ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [
            {"id": "s", "name": "s", "description": "s", "tags": ["t"]}
        ],
    }


@pytest.fixture
def dataset(lite_app):
    name = "ds_" + uuid.uuid4().hex[:8]
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    yield name
    lite_app.delete(f"/api/datasets/{name}")


# ── SDK happy paths (must succeed in lite) ───────────────────────────────────


def test_create_dataset(lite_app):
    name = "ds_" + uuid.uuid4().hex[:8]
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200
    body = r.json()
    assert body["dataset"] == name
    assert body["embedding_model"] == "all-MiniLM-L6-v2"
    assert body["formats"]["a2a"] == "v0.0"
    lite_app.delete(f"/api/datasets/{name}")


def test_register_a2a(lite_app, dataset):
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": _agent_card(), "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]
    assert sid


def test_list_get_update_deregister(lite_app, dataset):
    sid = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": _agent_card("flow-1"), "persistent": True},
    ).json()["service_id"]

    # list
    r = lite_app.get(f"/api/datasets/{dataset}/services")
    assert r.status_code == 200
    assert any(e["id"] == sid for e in r.json())

    # get
    r = lite_app.get(f"/api/datasets/{dataset}/services/{sid}")
    assert r.status_code == 200
    assert r.json()["id"] == sid

    # update
    r = lite_app.put(
        f"/api/datasets/{dataset}/services/{sid}",
        json={"status": "busy"},
    )
    assert r.status_code == 200, r.text

    # filter list using updated status
    r = lite_app.get(f"/api/datasets/{dataset}/services?status=busy")
    assert r.status_code == 200
    assert any(e["id"] == sid for e in r.json())

    # deregister
    r = lite_app.delete(f"/api/datasets/{dataset}/services/{sid}")
    assert r.status_code == 200, r.text


def test_reservation_lifecycle(lite_app, dataset):
    # register two agents to reserve
    sids = []
    for i in range(2):
        sid = lite_app.post(
            f"/api/datasets/{dataset}/services/a2a",
            json={"agent_card": _agent_card(f"resv-{i}"), "persistent": True},
        ).json()["service_id"]
        sids.append(sid)

    # reserve
    r = lite_app.post(
        f"/api/datasets/{dataset}/reservations",
        json={"filters": {}, "n": 2, "ttl_seconds": 30},
    )
    assert r.status_code == 200, r.text
    holder = r.json()["holder_id"]
    assert len(r.json()["reservations"]) == 2

    # extend
    r = lite_app.post(
        f"/api/datasets/{dataset}/reservations/{holder}/extend",
        json={"ttl_seconds": 60},
    )
    assert r.status_code == 200

    # release a single sid
    r = lite_app.delete(
        f"/api/datasets/{dataset}/reservations/{holder}/{sids[0]}"
    )
    assert r.status_code == 200

    # release all remaining
    r = lite_app.delete(f"/api/datasets/{dataset}/reservations/{holder}")
    assert r.status_code == 200


def test_release_lease_self(lite_app, dataset):
    sid = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": _agent_card("self-lease"), "persistent": True},
    ).json()["service_id"]
    holder = lite_app.post(
        f"/api/datasets/{dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 30},
    ).json()["holder_id"]
    r = lite_app.delete(f"/api/datasets/{dataset}/services/{sid}/lease")
    assert r.status_code == 200
    assert r.json()["prev_holder_id"] == holder


def test_embedding_models_lite_safe(lite_app):
    """Static metadata route must return 200 without [vector] extras."""
    r = lite_app.get("/api/datasets/embedding-models")
    assert r.status_code == 200
    models = r.json()["models"]
    assert "all-MiniLM-L6-v2" in models


# ── Heavy paths (must 503 with structured install hint) ──────────────────────


def test_search_returns_503(lite_app, dataset):
    r = lite_app.post(
        "/api/search",
        json={"query": "x", "method": "vector", "dataset": dataset, "top_k": 3},
    )
    assert r.status_code == 503
    body = r.json()
    assert body["feature"] == "vector"
    assert body["extras"] == "vector"
    assert "pip install 'a2x-registry[vector]'" in body["detail"]


def test_search_judge_returns_503(lite_app):
    r = lite_app.post(
        "/api/search/judge",
        json={"query": "x", "services": []},
    )
    assert r.status_code == 503
    assert r.json()["extras"] == "vector"


def test_build_trigger_returns_503(lite_app, dataset):
    r = lite_app.post(
        f"/api/datasets/{dataset}/build", json={"resume": "no"}
    )
    assert r.status_code == 503
    assert r.json()["extras"] == "vector"


def test_build_status_works_in_lite(lite_app, dataset):
    """Build status reads ``_build_jobs`` dict; no extras needed."""
    r = lite_app.get(f"/api/datasets/{dataset}/build/status")
    assert r.status_code == 200
    assert r.json()["status"] == "idle"


def test_search_ws_returns_install_hint(lite_app, dataset):
    """WebSocket: server accepts, then sends an error JSON with the hint."""
    with lite_app.websocket_connect("/api/search/ws") as ws:
        # Server's `feature_flags.require("vector")` fires before reading
        # input — but FastAPI WS handler reads first in our code, so we need
        # to send something. Send a search request payload.
        ws.send_json({
            "query": "x", "method": "vector",
            "dataset": dataset, "top_k": 3,
        })
        msg = ws.receive_json()
        assert msg["type"] == "error"
        assert "pip install 'a2x-registry[vector]'" in msg["message"]
