"""Update (service field-level update) feature tests.

The SDK uses ``PUT /api/datasets/{ds}/services/{sid}`` to flip a
service between online / busy / offline. After update the filtered
list view must reflect the new status — that's how peer agents notice
the change.
"""

from __future__ import annotations


def test_update_status_and_filter(lite_app, dataset, agent_card):
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": agent_card("update-1"), "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    r = lite_app.put(
        f"/api/datasets/{dataset}/services/{sid}",
        json={"status": "busy"},
    )
    assert r.status_code == 200, r.text

    r = lite_app.get(f"/api/datasets/{dataset}/services?status=busy")
    assert r.status_code == 200
    assert any(e["id"] == sid for e in r.json())
