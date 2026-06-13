"""The RegistryService mutation hook fires for every write op.

Driven through the real backend (cluster_app wires
``set_on_mutation(cluster_store.on_local_mutation)``): a CRUD op should
stamp a version (register/update) or a tombstone (deregister) on the
cluster store, observable via ``GET /api/cluster/state``.
"""

from __future__ import annotations

import uuid


def _make_dataset(app) -> str:
    ds = "ds_" + uuid.uuid4().hex[:6]
    assert app.post("/api/datasets", json={"name": ds}).status_code == 200
    return ds


def test_generic_register_stamps_version(cluster_app):
    ds = _make_dataset(cluster_app)
    before = cluster_app.get("/api/cluster/state").json()["local_records"]
    r = cluster_app.post(
        f"/api/datasets/{ds}/services/generic",
        json={"name": "svc", "description": "d"},
    )
    assert r.status_code == 200
    after = cluster_app.get("/api/cluster/state").json()["local_records"]
    assert after == before + 1


def test_a2a_register_stamps_version(cluster_app):
    ds = _make_dataset(cluster_app)
    before = cluster_app.get("/api/cluster/state").json()["local_records"]
    card = {
        "name": "agent-" + uuid.uuid4().hex[:5], "description": "x",
        "url": "http://e.invalid", "version": "1.0", "protocolVersion": "0.0.1",
        "capabilities": {}, "defaultInputModes": ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [{"id": "s", "name": "s", "description": "s", "tags": ["t"]}],
    }
    r = cluster_app.post(f"/api/datasets/{ds}/services/a2a", json={"agent_card": card})
    assert r.status_code == 200, r.text
    after = cluster_app.get("/api/cluster/state").json()["local_records"]
    assert after == before + 1


def test_update_then_deregister_tombstones(cluster_app):
    ds = _make_dataset(cluster_app)
    sid = cluster_app.post(
        f"/api/datasets/{ds}/services/generic",
        json={"name": "svc", "description": "d"},
    ).json()["service_id"]

    # Update bumps the version (no error, still a local record).
    assert cluster_app.put(
        f"/api/datasets/{ds}/services/{sid}", json={"description": "d2"},
    ).status_code == 200
    st = cluster_app.get("/api/cluster/state").json()
    assert st["local_records"] >= 1

    # Deregister → tombstone, local record removed.
    tomb_before = st["tombstones"]
    assert cluster_app.delete(f"/api/datasets/{ds}/services/{sid}").status_code == 200
    st2 = cluster_app.get("/api/cluster/state").json()
    assert st2["tombstones"] == tomb_before + 1
