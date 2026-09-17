"""All 4 corners of the namespace-config × client-ttl matrix."""

from __future__ import annotations


# ── Corner 1: ns OFF + client none → permanent (backward compat) ────────

def test_disabled_ns_no_ttl_yields_permanent_service(
    lite_app, heartbeat_disabled_dataset, a2a_card_factory,
):
    card = a2a_card_factory("perm1")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_disabled_dataset},
    )
    assert r.status_code == 200
    body = r.json()
    # The schema includes lease_* fields but they MUST be None / absent for permanent.
    assert body.get("lease_ttl") in (None, 0)
    assert body.get("lease_expires_at") in (None, 0)


# ── Corner 2: ns OFF + client set → 400 heartbeat_not_supported ─────────

def test_disabled_ns_with_ttl_rejected(
    lite_app, heartbeat_disabled_dataset, a2a_card_factory,
):
    card = a2a_card_factory("rej1")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_disabled_dataset, "lease_ttl": 30},
    )
    assert r.status_code == 400
    body = r.json()["detail"]
    assert body["code"] == "heartbeat_not_supported"


# ── Corner 3: ns ON + client none → 400 ttl_required ─────────────────────

def test_enabled_ns_missing_ttl_rejected(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    card = a2a_card_factory("rej2")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset},
    )
    assert r.status_code == 400
    body = r.json()["detail"]
    assert body["code"] == "ttl_required"
    # Bounds must come back so SDK / user can react without a second round-trip.
    assert body["min_ttl"] == 5
    assert body["max_ttl"] == 60


# ── Corner 4: ns ON + client out-of-range → 400 ttl_out_of_range ────────

def test_enabled_ns_ttl_too_low(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    card = a2a_card_factory("rej3")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 1},
    )
    assert r.status_code == 400
    body = r.json()["detail"]
    assert body["code"] == "ttl_out_of_range"
    assert body["min_ttl"] == 5
    assert body["max_ttl"] == 60


def test_enabled_ns_ttl_too_high(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    card = a2a_card_factory("rej4")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 9999},
    )
    assert r.status_code == 400
    body = r.json()["detail"]
    assert body["code"] == "ttl_out_of_range"


# ── Corner 4 happy path ─────────────────────────────────────────────────

def test_enabled_ns_valid_ttl_grants_lease(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    card = a2a_card_factory("ok1")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 20},
    )
    assert r.status_code == 200
    body = r.json()
    assert body["lease_ttl"] == 20
    assert body["lease_expires_at"] is not None
    assert body["lease_expires_at"] > 0


def test_boundary_ttls_accepted(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    """Inclusive bounds — min_ttl and max_ttl both work."""
    card = a2a_card_factory("min")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 5},
    )
    assert r.status_code == 200, r.text

    card = a2a_card_factory("max")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 60},
    )
    assert r.status_code == 200, r.text


def test_generic_service_also_supports_lease_ttl(
    lite_app, heartbeat_enabled_dataset,
):
    """Heartbeat is type-agnostic — generic services work too."""
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/generic",
        json={
            "dataset": heartbeat_enabled_dataset,
            "name": "g1", "description": "d", "lease_ttl": 30,
        },
    )
    assert r.status_code == 200, r.text
    assert r.json()["lease_ttl"] == 30
