"""Deregistered (service removal) feature tests.

After ``DELETE /api/datasets/{ds}/services/{sid}`` the service must
disappear from the list view. Lite mode must support deregistration
because Agent Team workflows need it during agent shutdown.
"""

from __future__ import annotations


def test_deregister_service(lite_app, dataset, agent_card):
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": agent_card("dereg-1"), "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    r = lite_app.delete(f"/api/datasets/{dataset}/services/{sid}")
    assert r.status_code == 200, r.text

    r = lite_app.get(f"/api/datasets/{dataset}/services")
    assert r.status_code == 200
    assert not any(e["id"] == sid for e in r.json())
