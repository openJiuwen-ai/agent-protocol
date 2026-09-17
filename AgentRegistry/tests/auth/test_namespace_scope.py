"""Cross-namespace probes: provider X's token must be 403 on dataset Y."""

from __future__ import annotations


def test_provider_token_blocked_on_unowned_namespace(
    auth_initialized_app, auth_dataset, provider_token_other_ns, agent_card,
):
    client, _ = auth_initialized_app
    token_other, _other_ds = provider_token_other_ns
    headers_other = {"Authorization": f"Bearer {token_other}"}

    # Try to mutate in auth_dataset using a token scoped to other_ds → 403.
    card = agent_card("crossns-attack")
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=headers_other,
    )
    assert r.status_code == 403


def test_provider_token_works_on_owned_namespace(
    auth_initialized_app, provider_token_other_ns, agent_card,
):
    """Sanity: same token works in its OWN namespace."""
    client, _ = auth_initialized_app
    token_other, other_ds = provider_token_other_ns
    headers_other = {"Authorization": f"Bearer {token_other}"}

    card = agent_card("self-ns-ok")
    r = client.post(
        f"/api/datasets/{other_ds}/services/a2a",
        json={"agent_card": card, "dataset": other_ds, "persistent": True},
        headers=headers_other,
    )
    assert r.status_code == 200


def test_provider_token_blocked_on_read_too(
    auth_initialized_app, auth_dataset, provider_token_other_ns,
):
    """Strict reads: even a list/get on an out-of-scope namespace is 403."""
    client, _ = auth_initialized_app
    token_other, _ = provider_token_other_ns
    headers_other = {"Authorization": f"Bearer {token_other}"}
    r = client.get(f"/api/datasets/{auth_dataset}/services", headers=headers_other)
    assert r.status_code == 403
