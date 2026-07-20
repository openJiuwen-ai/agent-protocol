"""User can list / get / reserve / release-own — and nothing else."""

from __future__ import annotations


def test_user_can_list_in_own_namespace(
    auth_initialized_app, auth_dataset, user_headers, provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    # Provider registers an entry so the listing isn't empty.
    card = agent_card("u1")
    client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    r = client.get(f"/api/datasets/{auth_dataset}/services", headers=user_headers)
    assert r.status_code == 200
    assert len(r.json()) >= 1


def test_user_cannot_register_service(
    auth_initialized_app, auth_dataset, user_headers, agent_card,
):
    client, _ = auth_initialized_app
    card = agent_card("user-attempt")
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=user_headers,
    )
    # The owner-write check runs after the namespace check, but the role
    # gate (provider/admin only) lands first → 403 either way.
    assert r.status_code == 403


def test_user_cannot_update_service(
    auth_initialized_app, auth_dataset, user_headers, provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    card = agent_card("u-update")
    sid = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    ).json()["service_id"]

    r = client.put(
        f"/api/datasets/{auth_dataset}/services/{sid}",
        json={"description": "hacked"}, headers=user_headers,
    )
    assert r.status_code == 403


def test_user_can_reserve_and_release_own(
    auth_initialized_app, auth_dataset, user_headers, provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    card = agent_card("reservable")
    card["status"] = "online"
    client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    r = client.post(
        f"/api/datasets/{auth_dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 60},
        headers=user_headers,
    )
    assert r.status_code == 200, r.text
    holder_id = r.json()["holder_id"]
    # User can release their own lease.
    r = client.delete(
        f"/api/datasets/{auth_dataset}/reservations/{holder_id}",
        headers=user_headers,
    )
    assert r.status_code == 200


def test_user_cannot_release_another_holders_lease(
    auth_initialized_app, auth_dataset, user_headers, provider_headers, admin_headers, agent_card,
):
    client, _ = auth_initialized_app
    # Create a service.
    card = agent_card("contested")
    card["status"] = "online"
    client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    # Provider reserves it (treated as a different holder than the user).
    rsv = client.post(
        f"/api/datasets/{auth_dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 60},
        headers=provider_headers,
    )
    assert rsv.status_code == 200
    p_holder = rsv.json()["holder_id"]
    # User tries to release provider's lease via the bulk-by-holder endpoint.
    r = client.delete(
        f"/api/datasets/{auth_dataset}/reservations/{p_holder}",
        headers=user_headers,
    )
    assert r.status_code == 403


def test_user_cannot_create_or_delete_dataset(
    auth_initialized_app, user_headers, auth_dataset,
):
    client, _ = auth_initialized_app
    # Create with auth_required=true → 403 (admin role required).
    r = client.post(
        "/api/datasets",
        json={"name": "user_made", "auth_required": True},
        headers=user_headers,
    )
    assert r.status_code == 403
    # Delete an existing auth-required dataset → 403.
    r = client.delete(f"/api/datasets/{auth_dataset}", headers=user_headers)
    assert r.status_code == 403
