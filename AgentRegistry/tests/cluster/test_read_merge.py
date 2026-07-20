"""Read-path merge: list/get surface replicated records from peers.

Foreign records are injected via the /updates endpoint (as a peer would),
then observed through the public dataset list/get endpoints.
"""

from __future__ import annotations

import uuid


def _foreign_env(origin, dataset, name, sid, desc="d"):
    return {
        "dataset": dataset, "service_id": sid, "origin_id": origin,
        "version": [1000, origin], "tombstone": False,
        "payload": {
            "entry": {
                "service_id": sid, "type": "generic", "source": "api_config",
                "service_data": {"name": name, "description": desc,
                                 "inputSchema": {}, "url": None},
            },
            "wrapped": {"id": sid, "type": "generic", "name": name,
                        "description": desc, "metadata": {}},
        },
    }


def _push(app, env):
    r = app.post("/api/cluster/updates", json={"from_node": "A", "envelopes": [env]})
    assert r.status_code == 200, r.text


def test_list_merges_local_and_foreign(cluster_app):
    ds = "ds_" + uuid.uuid4().hex[:6]
    assert cluster_app.post("/api/datasets", json={"name": ds}).status_code == 200
    local_sid = cluster_app.post(
        f"/api/datasets/{ds}/services/generic",
        json={"name": "local-svc", "description": "d"},
    ).json()["service_id"]

    _push(cluster_app, _foreign_env("A", ds, "remote-svc", "generic_remote"))

    rows = cluster_app.get(f"/api/datasets/{ds}/services").json()
    by_id = {r["id"]: r for r in rows}
    assert local_sid in by_id
    assert "A:generic_remote" in by_id
    foreign = by_id["A:generic_remote"]
    assert foreign["name"] == "remote-svc"
    assert foreign["origin_id"] == "A"
    assert foreign["source"] == "cluster"
    # Local row is unchanged (no origin_id injected).
    assert "origin_id" not in by_id[local_sid]


def test_get_single_foreign_by_namespaced_id(cluster_app):
    ds = "ds_" + uuid.uuid4().hex[:6]
    cluster_app.post("/api/datasets", json={"name": ds})
    _push(cluster_app, _foreign_env("A", ds, "remote-svc", "generic_remote"))

    r = cluster_app.get(f"/api/datasets/{ds}/services/A:generic_remote")
    assert r.status_code == 200
    body = r.json()
    assert body["name"] == "remote-svc"
    assert body["origin_id"] == "A"

    # A non-existent namespaced id still 404s.
    assert cluster_app.get(f"/api/datasets/{ds}/services/A:nope").status_code == 404


def test_filter_applies_to_foreign(cluster_app):
    ds = "ds_" + uuid.uuid4().hex[:6]
    cluster_app.post("/api/datasets", json={"name": ds})
    cluster_app.post(f"/api/datasets/{ds}/services/generic",
                     json={"name": "local-svc", "description": "d"})
    _push(cluster_app, _foreign_env("A", ds, "remote-svc", "generic_remote"))

    rows = cluster_app.get(f"/api/datasets/{ds}/services", params={"name": "remote-svc"}).json()
    assert len(rows) == 1
    assert rows[0]["id"] == "A:generic_remote"


def test_same_name_local_and_foreign_coexist(cluster_app):
    ds = "ds_" + uuid.uuid4().hex[:6]
    cluster_app.post("/api/datasets", json={"name": ds})
    local_sid = cluster_app.post(
        f"/api/datasets/{ds}/services/generic",
        json={"name": "translator", "description": "d"},
    ).json()["service_id"]
    # Foreign uses the SAME service_id (same name hash) but different origin.
    _push(cluster_app, _foreign_env("A", ds, "translator", local_sid))

    rows = cluster_app.get(f"/api/datasets/{ds}/services").json()
    ids = {r["id"] for r in rows}
    assert local_sid in ids
    assert f"A:{local_sid}" in ids
    assert len(rows) == 2


def test_tombstoned_foreign_not_listed(cluster_app):
    ds = "ds_" + uuid.uuid4().hex[:6]
    cluster_app.post("/api/datasets", json={"name": ds})
    _push(cluster_app, _foreign_env("A", ds, "remote-svc", "generic_remote"))
    # Now a tombstone with a higher version → record disappears from listing.
    tomb = {"dataset": ds, "service_id": "generic_remote", "origin_id": "A",
            "version": [2000, "A"], "tombstone": True, "payload": None}
    _push(cluster_app, tomb)

    rows = cluster_app.get(f"/api/datasets/{ds}/services").json()
    assert all(r["id"] != "A:generic_remote" for r in rows)
