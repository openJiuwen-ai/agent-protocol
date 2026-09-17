"""``owner_id`` must not be overwritable via the PUT updates body."""

from __future__ import annotations


def test_put_owner_id_field_is_filtered_silently(
    auth_initialized_app, auth_dataset, provider_headers, agent_card,
):
    """Provider includes owner_id in PUT body; server strips it silently
    and persists the original owner_id unchanged."""
    client, _ = auth_initialized_app
    card = agent_card("immutable-owner")
    sid = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    ).json()["service_id"]

    me = client.get("/api/auth/whoami", headers=provider_headers).json()
    original_owner = me["principal_id"]

    # Attempt the relabel attack: include owner_id in the updates dict.
    r = client.put(
        f"/api/datasets/{auth_dataset}/services/{sid}",
        json={
            "description": "ok change",
            "owner_id": "u_attacker_evil_id",   # MUST be stripped
            "service_id": "different",          # MUST be stripped
            "type": "skill",                    # MUST be stripped
            "source": "user_config",            # MUST be stripped
        },
        headers=provider_headers,
    )
    assert r.status_code == 200, r.text

    # Verify the persisted entry's owner_id is unchanged.
    detail = client.get(
        f"/api/datasets/{auth_dataset}/services/{sid}", headers=provider_headers,
    ).json()
    md = detail.get("metadata") or {}
    persisted_owner = md.get("owner_id") or detail.get("owner_id")
    assert persisted_owner == original_owner, (
        f"owner_id was relabeled! original={original_owner!r}, "
        f"persisted={persisted_owner!r}"
    )
