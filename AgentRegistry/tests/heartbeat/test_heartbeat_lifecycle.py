"""End-to-end lease lifecycle: register → heartbeat → expire → recover → hard delete."""

from __future__ import annotations

import time


def _register_with_lease(lite_app, dataset, ttl, card_factory, name="svc"):
    card = card_factory(name)
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": card, "dataset": dataset, "lease_ttl": ttl},
    )
    assert r.status_code == 200, r.text
    return r.json()["service_id"]


def test_heartbeat_extends_lease(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    sid = _register_with_lease(lite_app, heartbeat_enabled_dataset, 20, a2a_card_factory)
    lease_before = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    expires_before = lease_before.expires_at

    # Need to wait a beat so the new expires_at is detectably different.
    time.sleep(0.01)
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat", json={},
    )
    assert r.status_code == 200
    lease_after = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    assert lease_after.expires_at > expires_before


def test_heartbeat_on_missing_lease_yields_404(
    lite_app, heartbeat_disabled_dataset, a2a_card_factory,
):
    """A permanent service (no lease) heartbeat → 404."""
    card = a2a_card_factory("p1")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_disabled_dataset},
    )
    sid = r.json()["service_id"]
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/{sid}/heartbeat",
        json={},
    )
    assert r.status_code == 404


def test_expire_then_recover_keeps_metadata(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """Drive time forward via sweep_tick; lease goes UNHEALTHY; heartbeat
    during grace recovers without losing the entry / sid."""
    sid = _register_with_lease(lite_app, heartbeat_enabled_dataset, 5, a2a_card_factory)
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    # Drive a tick that's past expires_at but within grace
    newly_unhealthy, to_delete = heartbeat_store.sweep_tick(now=lease.expires_at + 0.001)
    assert (heartbeat_enabled_dataset, sid) in newly_unhealthy
    assert to_delete == []

    # Service is now hidden from default list_services
    listed = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert all(s["id"] != sid for s in listed)
    # But shows with ?include_unhealthy=true
    listed_all = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    assert any(s["id"] == sid for s in listed_all)

    # Recovery via heartbeat
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat", json={},
    )
    assert r.status_code == 200
    assert heartbeat_store.is_unhealthy(heartbeat_enabled_dataset, sid) is False
    listed = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert any(s["id"] == sid for s in listed)


def test_expire_past_grace_triggers_hard_delete(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    sid = _register_with_lease(lite_app, heartbeat_enabled_dataset, 5, a2a_card_factory)
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    # Tick past grace_deadline
    newly_unhealthy, to_delete = heartbeat_store.sweep_tick(now=lease.grace_deadline + 0.001)
    assert (heartbeat_enabled_dataset, sid) in to_delete

    # Need to actually do the deregister — sweep_tick only returns the list;
    # the production sweeper calls deregister. Simulate that here.
    from a2x_registry.heartbeat.system_ctx import SYSTEM_CTX
    from a2x_registry.backend.routers.dataset import get_registry_service
    svc = get_registry_service()
    svc.deregister(heartbeat_enabled_dataset, sid, caller=SYSTEM_CTX)

    # Entry is gone — even with include_unhealthy=true
    listed_all = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    assert all(s["id"] != sid for s in listed_all)


def test_heartbeat_with_status_piggyback(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    sid = _register_with_lease(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory)
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat",
        json={"status": "busy"},
    )
    assert r.status_code == 200
    # Read entry and verify status was applied
    detail = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}",
    ).json()
    md = detail.get("metadata") or {}
    assert md.get("status") == "busy" or detail.get("status") == "busy"
