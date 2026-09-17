"""Provider can register / mutate / deregister OWN services in OWN namespaces."""

from __future__ import annotations


def test_provider_registers_with_owner_id_set(
    auth_initialized_app, admin_headers, auth_dataset,
    provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    card = agent_card("by-provider")
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    # The persisted entry should carry an owner_id matching the provider's principal_id.
    me = client.get("/api/auth/whoami", headers=provider_headers).json()
    detail = client.get(
        f"/api/datasets/{auth_dataset}/services/{sid}", headers=provider_headers,
    ).json()
    # owner_id is exposed in the metadata block at this layer (service.py writes
    # it into the entry; the wrapper surfaces it under metadata).
    md = detail.get("metadata") or {}
    assert md.get("owner_id") == me["principal_id"] or detail.get("owner_id") == me["principal_id"]


def test_provider_can_update_own_service(
    auth_initialized_app, auth_dataset, provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    card = agent_card("self-edit")
    sid = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    ).json()["service_id"]

    r = client.put(
        f"/api/datasets/{auth_dataset}/services/{sid}",
        json={"description": "edited"}, headers=provider_headers,
    )
    assert r.status_code == 200


def test_provider_cannot_mutate_others_services(
    auth_initialized_app, admin_headers, auth_dataset, provider_headers, agent_card,
):
    """A second provider registers; first provider tries to update — 403."""
    client, _ = auth_initialized_app
    # Make a second provider in the same namespace.
    p2 = client.post(
        "/api/auth/principals",
        json={"handle": "p2", "role": "provider", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()
    p2_headers = {"Authorization": f"Bearer {p2['token']}"}

    # p2 registers a service.
    card = agent_card("p2-owned")
    sid = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=p2_headers,
    ).json()["service_id"]

    # Original provider tries to update — must 403.
    r = client.put(
        f"/api/datasets/{auth_dataset}/services/{sid}",
        json={"description": "hijacked"}, headers=provider_headers,
    )
    assert r.status_code == 403


def test_provider_can_deregister_own_service(
    auth_initialized_app, auth_dataset, provider_headers, agent_card,
):
    client, _ = auth_initialized_app
    card = agent_card("to-delete")
    sid = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    ).json()["service_id"]

    r = client.delete(
        f"/api/datasets/{auth_dataset}/services/{sid}", headers=provider_headers,
    )
    assert r.status_code == 200


def test_provider_cannot_toggle_auth_config(
    auth_initialized_app, auth_dataset, provider_headers,
):
    client, _ = auth_initialized_app
    r = client.post(
        f"/api/datasets/{auth_dataset}/auth-config",
        json={"required": False}, headers=provider_headers,
    )
    assert r.status_code == 403


def test_provider_cannot_delete_dataset(
    auth_initialized_app, auth_dataset, provider_headers,
):
    client, _ = auth_initialized_app
    r = client.delete(f"/api/datasets/{auth_dataset}", headers=provider_headers)
    assert r.status_code == 403
