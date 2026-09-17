"""Admin can do everything anywhere, including mutating other principals' services."""

from __future__ import annotations


def test_admin_can_register_in_auth_required_ns(
    auth_initialized_app, admin_headers, auth_dataset, agent_card
):
    client, _ = auth_initialized_app
    card = agent_card("by-admin")
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=admin_headers,
    )
    assert r.status_code == 200, r.text


def test_admin_can_overwrite_provider_service(
    auth_initialized_app, admin_headers, auth_dataset,
    provider_headers, agent_card,
):
    """Provider registers; admin updates — short-circuits the owner check."""
    client, _ = auth_initialized_app
    card = agent_card("admin-overwrite")
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    r = client.put(
        f"/api/datasets/{auth_dataset}/services/{sid}",
        json={"description": "admin updated"}, headers=admin_headers,
    )
    assert r.status_code == 200


def test_admin_can_delete_dataset(
    auth_initialized_app, admin_headers
):
    """Admin can delete an auth-required dataset (the only legal way)."""
    client, _ = auth_initialized_app
    r = client.post(
        "/api/datasets",
        json={"name": "del_target", "auth_required": True},
        headers=admin_headers,
    )
    assert r.status_code == 200
    r = client.delete("/api/datasets/del_target", headers=admin_headers)
    assert r.status_code == 200


def test_admin_can_release_others_reservation(
    auth_initialized_app, admin_headers, auth_dataset,
    user_headers, provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    # Provider registers an entry first so reservations have something to claim.
    card = agent_card("targetable")
    card["status"] = "online"
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    assert r.status_code == 200, r.text

    # User reserves it.
    rsv = client.post(
        f"/api/datasets/{auth_dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 60},
        headers=user_headers,
    )
    assert rsv.status_code == 200, rsv.text
    holder_id = rsv.json()["holder_id"]

    # Admin can release the user's lease.
    r = client.delete(
        f"/api/datasets/{auth_dataset}/reservations/{holder_id}",
        headers=admin_headers,
    )
    assert r.status_code == 200
