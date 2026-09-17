"""Deregistered (lease auto-release) feature tests.

A service that holds a reservation lease can self-release through
``DELETE /api/datasets/{ds}/services/{sid}/lease`` — the response
must echo the previous holder id so the caller can audit the change.
"""

from __future__ import annotations


def test_release_lease_self(lite_app, dataset, agent_card):
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": agent_card("self-lease"), "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    r = lite_app.post(
        f"/api/datasets/{dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 30},
    )
    assert r.status_code == 200, r.text
    holder = r.json()["holder_id"]

    r = lite_app.delete(f"/api/datasets/{dataset}/services/{sid}/lease")
    assert r.status_code == 200
    assert r.json()["prev_holder_id"] == holder
