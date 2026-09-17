"""Unhealthy entries are filtered from list/reserve by default."""

from __future__ import annotations

import time
import uuid


def _register(lite_app, dataset, ttl, card_factory, name=None):
    card = card_factory(name or f"u_{uuid.uuid4().hex[:6]}")
    r = lite_app.post(
        f"/api/datasets/{dataset}/services/a2a",
        json={"agent_card": card, "dataset": dataset, "lease_ttl": ttl},
    )
    return r.json()["service_id"]


def _mark_unhealthy(store, dataset, sid):
    """Push (ds, sid) into UNHEALTHY state by mutating expires_at."""
    lease = store.get_lease(dataset, sid)
    lease.expires_at = time.monotonic() - 0.1
    store.sweep_tick(now=time.monotonic())


def test_list_services_filters_unhealthy_by_default(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    sid = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="u1")
    _mark_unhealthy(heartbeat_store, heartbeat_enabled_dataset, sid)
    listed = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert all(s["id"] != sid for s in listed)


def test_list_services_include_unhealthy_true_surfaces_them(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    sid = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="u2")
    _mark_unhealthy(heartbeat_store, heartbeat_enabled_dataset, sid)
    listed = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    assert any(s["id"] == sid for s in listed)


def test_healthy_entries_appear_in_default_list(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    """Sanity: heartbeat enabled but service has fresh lease → listed normally."""
    sid = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="u3")
    listed = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert any(s["id"] == sid for s in listed)


def test_reserve_skips_unhealthy(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """Reservation candidate selection must filter unhealthy entries too."""
    # Register two services — one becomes unhealthy.
    sid_a = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="rsva")
    sid_b = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="rsvb")
    _mark_unhealthy(heartbeat_store, heartbeat_enabled_dataset, sid_a)

    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/reservations",
        json={"filters": {}, "n": 2, "ttl_seconds": 30},
    )
    assert r.status_code == 200
    claimed = [s["id"] for s in r.json()["reservations"]]
    assert sid_a not in claimed
    assert sid_b in claimed


def test_filter_flags_are_independent(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """include_unhealthy and include_leased filter independently — can be set on either side."""
    # Two unhealthy entries, neither leased
    sid1 = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="i1")
    sid2 = _register(lite_app, heartbeat_enabled_dataset, 30, a2a_card_factory, name="i2")
    _mark_unhealthy(heartbeat_store, heartbeat_enabled_dataset, sid1)
    _mark_unhealthy(heartbeat_store, heartbeat_enabled_dataset, sid2)

    # Default: both filtered
    r = lite_app.get(f"/api/datasets/{heartbeat_enabled_dataset}/services").json()
    assert all(s["id"] not in {sid1, sid2} for s in r)
    # include_unhealthy=true: both visible
    r = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    listed_ids = {s["id"] for s in r}
    assert {sid1, sid2}.issubset(listed_ids)
