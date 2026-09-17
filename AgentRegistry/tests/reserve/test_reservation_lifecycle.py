"""Reserve (reservation lease lifecycle) feature tests.

A reservation lease lets one caller hold N services from a dataset
for a TTL. Lifecycle:

  reserve (n=2)              → returns holder_id + reservation list
  extend  (ttl_seconds=60)   → bumps TTL on every entry the holder owns
  release one sid            → drops a single reservation
  release all under holder   → clears whatever remains

All four steps must succeed in lite mode because Agent Team
coordination depends on them.
"""

from __future__ import annotations


def test_reservation_lifecycle(lite_app, dataset, agent_card):
    sids = []
    for i in range(2):
        r = lite_app.post(
            f"/api/datasets/{dataset}/services/a2a",
            json={"agent_card": agent_card(f"resv-{i}"), "persistent": True},
        )
        assert r.status_code == 200, r.text
        sids.append(r.json()["service_id"])

    r = lite_app.post(
        f"/api/datasets/{dataset}/reservations",
        json={"filters": {}, "n": 2, "ttl_seconds": 30},
    )
    assert r.status_code == 200, r.text
    holder = r.json()["holder_id"]
    assert len(r.json()["reservations"]) == 2

    r = lite_app.post(
        f"/api/datasets/{dataset}/reservations/{holder}/extend",
        json={"ttl_seconds": 60},
    )
    assert r.status_code == 200

    r = lite_app.delete(
        f"/api/datasets/{dataset}/reservations/{holder}/{sids[0]}"
    )
    assert r.status_code == 200

    r = lite_app.delete(f"/api/datasets/{dataset}/reservations/{holder}")
    assert r.status_code == 200
