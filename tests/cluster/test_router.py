"""HTTP-level smoke: the /api/cluster/* router on the receiver side.

A simulated peer "A" drives B's endpoints over the TestClient, validating
that the router delegates to the store handlers correctly.
"""

from __future__ import annotations

import uuid


def test_state_endpoint_reports_node_id(cluster_app):
    r = cluster_app.get("/api/cluster/state")
    assert r.status_code == 200
    assert r.json()["node_id"] == "B"


def test_session_digest_pull_flow(cluster_app):
    # B hosts a dataset + a service.
    ds = "ds_" + uuid.uuid4().hex[:6]
    assert cluster_app.post("/api/datasets", json={"name": ds}).status_code == 200
    r = cluster_app.post(
        f"/api/datasets/{ds}/services/generic",
        json={"name": "b-svc", "description": "hello"},
    )
    sid = r.json()["service_id"]

    # Peer A opens a session (no auth → anon namespace accepted).
    r = cluster_app.post(
        "/api/cluster/sessions",
        json={"node_id": "A", "address": "http://a", "namespaces": [], "token": None},
    )
    assert r.status_code == 200
    assert ds in r.json()["accepted"]

    # A pulls B's digest → sees B's record.
    r = cluster_app.get("/api/cluster/digest", params={"from_node": "A", "namespaces": ds})
    assert r.status_code == 200
    rows = r.json()
    keys = {(row[0], row[1], row[2]) for row in rows}
    assert (ds, "B", sid) in keys

    # A pulls the full envelope.
    r = cluster_app.post(
        "/api/cluster/pulls",
        json={"from_node": "A", "keys": [[ds, "B", sid]]},
    )
    assert r.status_code == 200
    envs = r.json()
    assert len(envs) == 1
    assert envs[0]["origin_id"] == "B"
    assert envs[0]["payload"]["wrapped"]["name"] == "b-svc"


def test_updates_endpoint_accepts_foreign_record(cluster_app):
    # A pushes one of its own records into B.
    env = {
        "dataset": "remote_ds", "service_id": "generic_x", "origin_id": "A",
        "version": [1000, "A"], "tombstone": False,
        "payload": {
            "entry": {"service_id": "generic_x", "type": "generic", "source": "api_config",
                      "service_data": {"name": "a-svc", "description": "d", "inputSchema": {}, "url": None}},
            "wrapped": {"id": "generic_x", "type": "generic", "name": "a-svc",
                        "description": "d", "metadata": {}},
        },
    }
    r = cluster_app.post(
        "/api/cluster/updates", json={"from_node": "A", "envelopes": [env]},
    )
    assert r.status_code == 200
    assert r.json()["accepted"] == 1

    # Re-posting the same version is idempotent (dedup).
    r = cluster_app.post(
        "/api/cluster/updates", json={"from_node": "A", "envelopes": [env]},
    )
    assert r.json()["accepted"] == 0

    # It shows up in the state summary.
    assert cluster_app.get("/api/cluster/state").json()["foreign_records"] == 1
