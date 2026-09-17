"""Per-namespace ``auth_required`` toggle behavior + orphan-entry semantics."""

from __future__ import annotations


def test_get_auth_config_public(lite_app, dataset):
    """GET /auth-config doesn't require any auth — it's metadata."""
    r = lite_app.get(f"/api/datasets/{dataset}/auth-config")
    assert r.status_code == 200
    body = r.json()
    assert body["dataset"] == dataset
    assert body["required"] is False  # default for legacy datasets


def test_toggle_on_via_admin(auth_initialized_app, admin_headers, anon_dataset):
    client, _ = auth_initialized_app
    # Initially anon.
    r = client.get(f"/api/datasets/{anon_dataset}/auth-config")
    assert r.json()["required"] is False
    # Toggle on.
    r = client.post(
        f"/api/datasets/{anon_dataset}/auth-config",
        json={"required": True}, headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    assert r.json()["required"] is True
    # Read-back confirms persistence.
    r = client.get(f"/api/datasets/{anon_dataset}/auth-config")
    assert r.json()["required"] is True


def test_toggle_off_via_admin(auth_initialized_app, admin_headers, auth_dataset):
    client, _ = auth_initialized_app
    r = client.post(
        f"/api/datasets/{auth_dataset}/auth-config",
        json={"required": False}, headers=admin_headers,
    )
    assert r.status_code == 200
    assert r.json()["required"] is False


def test_toggle_requires_admin(
    auth_initialized_app, provider_headers, auth_dataset
):
    client, _ = auth_initialized_app
    r = client.post(
        f"/api/datasets/{auth_dataset}/auth-config",
        json={"required": False}, headers=provider_headers,
    )
    assert r.status_code == 403


def test_create_dataset_with_auth_required_needs_admin_token(
    auth_initialized_app
):
    """auth_required=true without admin token → 401 (not authenticated)."""
    client, _ = auth_initialized_app
    r = client.post(
        "/api/datasets",
        json={"name": "needs_admin", "auth_required": True},
    )
    assert r.status_code == 401


def test_create_dataset_with_auth_required_provider_rejected(
    auth_initialized_app, provider_headers
):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/datasets",
        json={"name": "needs_admin2", "auth_required": True},
        headers=provider_headers,
    )
    assert r.status_code == 403


def test_create_dataset_with_auth_required_pre_bootstrap_returns_409(lite_app):
    """Without auth init, the operator gets an actionable 409 — not a silent
    1-true-namespace registry, and not a confusing 401."""
    r = lite_app.post(
        "/api/datasets",
        json={"name": "early_bird", "auth_required": True},
    )
    assert r.status_code == 409
    assert "auth_not_initialized" in r.text.lower() or "auth init" in r.text.lower()


def test_orphan_entries_after_toggle_on(
    auth_initialized_app, admin_headers, anon_dataset, agent_card
):
    """Toggle a dataset from anon → auth-required after entries exist.
    Those entries have owner_id=None and only admin can mutate them."""
    client, _ = auth_initialized_app
    # Register one A2A entry anonymously while still anon.
    card = agent_card("orphan")
    r = client.post(
        f"/api/datasets/{anon_dataset}/services/a2a",
        json={"agent_card": card, "dataset": anon_dataset, "persistent": True},
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    # Toggle auth on.
    r = client.post(
        f"/api/datasets/{anon_dataset}/auth-config",
        json={"required": True}, headers=admin_headers,
    )
    assert r.status_code == 200

    # Provision a provider with this namespace.
    p = client.post(
        "/api/auth/principals",
        json={"handle": "p_for_orphan", "role": "provider",
              "namespaces": [anon_dataset]},
        headers=admin_headers,
    ).json()
    provider_h = {"Authorization": f"Bearer {p['token']}"}

    # Provider tries to update the orphan → 403 (system / unclaimed service).
    r = client.put(
        f"/api/datasets/{anon_dataset}/services/{sid}",
        json={"description": "hijacked"}, headers=provider_h,
    )
    assert r.status_code == 403
    assert "admin" in r.text.lower() or "unclaim" in r.text.lower()

    # Admin can still mutate.
    r = client.put(
        f"/api/datasets/{anon_dataset}/services/{sid}",
        json={"description": "admin-rescue"}, headers=admin_headers,
    )
    assert r.status_code == 200
