"""DELETE /heartbeat — soft revoke (default) vs permanent."""

from __future__ import annotations


def _register(lite_app, dataset, ttl, card_factory, name="rev"):
    card = card_factory(name)
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": card, "dataset": dataset, "lease_ttl": ttl},
    )
    return r.json()["service_id"]


def test_revoke_soft_marks_unhealthy_immediately(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    sid = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory)
    r = lite_app.request(
        "DELETE",
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat",
        json={"permanent": False},
    )
    assert r.status_code == 200
    body = r.json()
    assert body["permanent"] is False
    assert heartbeat_store.is_unhealthy(heartbeat_enabled_dataset, sid) is True

    # Default list filters it out, but include_unhealthy=true surfaces it.
    listed = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert all(s["id"] != sid for s in listed)
    listed_all = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    assert any(s["id"] == sid for s in listed_all)


def test_revoke_soft_is_recoverable_during_grace(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    """Soft revoke is reversible — a heartbeat during grace returns to HEALTHY."""
    sid = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory)
    lite_app.request(
        "DELETE",
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat",
        json={"permanent": False},
    )
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat", json={},
    )
    assert r.status_code == 200
    listed = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert any(s["id"] == sid for s in listed)


def test_revoke_permanent_drops_entry_entirely(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    sid = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory)
    r = lite_app.request(
        "DELETE",
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat",
        json={"permanent": True},
    )
    assert r.status_code == 200

    # Entry is gone — even with include_unhealthy.
    listed = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    assert all(s["id"] != sid for s in listed)


def test_revoke_nonexistent_lease_404(
    lite_app, heartbeat_enabled_dataset,
):
    r = lite_app.request(
        "DELETE",
        f"/api/datasets/{heartbeat_enabled_dataset}/services/no_such_sid/heartbeat",
        json={"permanent": False},
    )
    assert r.status_code == 404
