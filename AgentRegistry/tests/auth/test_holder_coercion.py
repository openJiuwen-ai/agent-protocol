"""On auth-required namespaces, ``holder_id`` in the reservation body is
overridden with the caller's principal_id (T2 in the threat model)."""

from __future__ import annotations


def test_request_body_holder_id_is_overridden(
    auth_initialized_app, auth_dataset, provider_headers, user_headers, agent_card,
):
    """User reserves with body holder_id='attacker'; server uses user.principal_id."""
    client, _ = auth_initialized_app

    # Register an entry to reserve.
    card = agent_card("coerce-target")
    card["status"] = "online"
    client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )

    me_user = client.get("/api/auth/whoami", headers=user_headers).json()
    user_id = me_user["principal_id"]

    # Reserve with a forged holder_id.
    r = client.post(
        f"/api/datasets/{auth_dataset}/reservations",
        json={
            "filters": {}, "n": 1, "ttl_seconds": 60,
            "holder_id": "attacker_forgery",
        },
        headers=user_headers,
    )
    assert r.status_code == 200, r.text
    assert r.json()["holder_id"] == user_id, (
        "holder_id in response must equal caller principal_id, not request body"
    )


def test_release_other_holder_id_in_path_blocked(
    auth_initialized_app, auth_dataset, provider_headers, user_headers, agent_card,
):
    """User cannot release using a path holder_id that isn't their principal_id."""
    client, _ = auth_initialized_app

    # Register + reserve via provider.
    card = agent_card("release-target")
    card["status"] = "online"
    client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    rsv = client.post(
        f"/api/datasets/{auth_dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 60},
        headers=provider_headers,
    )
    assert rsv.status_code == 200, rsv.text
    provider_holder = rsv.json()["holder_id"]

    # User attempts to release with path holder_id = provider's.
    r = client.delete(
        f"/api/datasets/{auth_dataset}/reservations/{provider_holder}",
        headers=user_headers,
    )
    assert r.status_code == 403
