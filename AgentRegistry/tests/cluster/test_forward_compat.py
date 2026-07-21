"""Forward-compat: with the cluster module NOT initialized, the registry
behaves exactly as before.

``lite_app`` boots with a tmp home that has no ``cluster_state.json``, so
``ClusterStore.load_or_none`` returns None and the feature is dormant:
- every ``/api/cluster/*`` route 404s,
- register / list / get behave identically to pre-cluster.
"""

from __future__ import annotations

import uuid


def test_cluster_routes_404_when_not_initialized(lite_app):
    r = lite_app.get("/api/cluster/state")
    assert r.status_code == 404
    assert "not initialized" in r.json()["detail"].lower()


def test_crud_and_list_unchanged_with_cluster_off(lite_app, dataset):
    # Register a generic service.
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/generic",
        json={"name": "svc-" + uuid.uuid4().hex[:6], "description": "d"},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    # List returns exactly the one local record — no foreign rows, no
    # ``origin_id`` injected (cluster is off).
    r = lite_app.get(f"/api/datasets/{dataset}/services")
    assert r.status_code == 200, r.text
    rows = r.json()
    assert len(rows) == 1
    assert rows[0]["id"] == sid
    assert "origin_id" not in rows[0]

    # Single get works and id is the plain (un-namespaced) service_id.
    r = lite_app.get(f"/api/datasets/{dataset}/services/{sid}")
    assert r.status_code == 200, r.text
    assert r.json()["id"] == sid
