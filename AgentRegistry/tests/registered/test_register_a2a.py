"""Registered (A2A registration) feature tests.

Covers the SDK registration path most frequently exercised by agents:
``POST /api/datasets/{ds}/services/a2a`` with an inline Agent Card and
``persistent=True``. The endpoint must succeed in lite mode and return
a stable service id that can be used by downstream list/get calls.
"""

from __future__ import annotations


def test_register_a2a(lite_app, dataset, agent_card):
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": agent_card(), "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]
    assert sid


def test_register_then_list_and_get(lite_app, dataset, agent_card):
    """After registration, the service must appear in list and get."""
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": agent_card("flow-1"), "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    r = lite_app.get(f"/api/datasets/{dataset}/services")
    assert r.status_code == 200
    assert any(e["id"] == sid for e in r.json())

    r = lite_app.get(f"/api/datasets/{dataset}/services/{sid}")
    assert r.status_code == 200
    assert r.json()["id"] == sid
