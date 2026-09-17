"""Heartbeat endpoints respect the existing auth gate (Depends(authorize))."""

from __future__ import annotations

import uuid

import pytest


@pytest.fixture
def auth_initialized_app(lite_app, tmp_path):
    """Mirror tests/auth/conftest.py — bootstrap auth on top of lite_app."""
    from a2x_registry.auth.store import AuthStore
    from a2x_registry.auth.deps import set_auth_store

    auth_data = tmp_path / "auth_data_hb"
    store, token = AuthStore.bootstrap(data_dir=auth_data)
    set_auth_store(store)
    try:
        yield lite_app, token
    finally:
        set_auth_store(None)


@pytest.fixture
def admin_headers(auth_initialized_app):
    _, token = auth_initialized_app
    return {"Authorization": f"Bearer {token}"}


@pytest.fixture
def auth_required_hb_dataset(auth_initialized_app, admin_headers):
    """Dataset with BOTH auth_required=true AND heartbeat enabled."""
    client, _ = auth_initialized_app
    name = "hbauth_" + uuid.uuid4().hex[:8]
    r = client.post(
        "/api/datasets",
        json={"name": name, "auth_required": True},
        headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    r = client.post(
        f"/api/datasets/{name}/lease-config",
        json={"enabled": True, "min_ttl": 5, "max_ttl": 60, "grace_period": 10},
        headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    yield name
    client.delete(f"/api/datasets/{name}", headers=admin_headers)


def _provider_token(client, admin_headers, ns):
    r = client.post(
        "/api/auth/principals",
        json={
            "handle": "hb_provider_" + uuid.uuid4().hex[:6],
            "role": "provider",
            "namespaces": [ns],
        },
        headers=admin_headers,
    )
    assert r.status_code == 201, r.text
    return r.json()["token"]


def test_heartbeat_without_token_on_auth_ns_yields_401(
    auth_initialized_app, admin_headers, auth_required_hb_dataset, a2a_card_factory,
):
    client, _ = auth_initialized_app
    # Provider registers a service with lease
    p_token = _provider_token(client, admin_headers, auth_required_hb_dataset)
    p_headers = {"Authorization": f"Bearer {p_token}"}
    card = a2a_card_factory("a")
    r = client.post(
        f"/api/datasets/{auth_required_hb_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_required_hb_dataset, "lease_ttl": 10},
        headers=p_headers,
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    # Heartbeat without token → 401
    r = client.post(
        f"/api/datasets/{auth_required_hb_dataset}/services/{sid}/heartbeat", json={},
    )
    assert r.status_code == 401


def test_provider_can_heartbeat_own_service(
    auth_initialized_app, admin_headers, auth_required_hb_dataset, a2a_card_factory,
):
    client, _ = auth_initialized_app
    p_token = _provider_token(client, admin_headers, auth_required_hb_dataset)
    p_headers = {"Authorization": f"Bearer {p_token}"}
    card = a2a_card_factory("b")
    r = client.post(
        f"/api/datasets/{auth_required_hb_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_required_hb_dataset, "lease_ttl": 10},
        headers=p_headers,
    )
    sid = r.json()["service_id"]
    r = client.post(
        f"/api/datasets/{auth_required_hb_dataset}/services/{sid}/heartbeat",
        json={}, headers=p_headers,
    )
    assert r.status_code == 200
