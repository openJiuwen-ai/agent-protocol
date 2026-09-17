"""POST/GET/PATCH /api/auth/principals — admin-only flows."""

from __future__ import annotations

import pytest


def test_create_provider_returns_plaintext_token_once(
    auth_initialized_app, admin_headers, auth_dataset
):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/auth/principals",
        json={"handle": "alice", "role": "provider", "namespaces": [auth_dataset]},
        headers=admin_headers,
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["handle"] == "alice"
    assert body["role"] == "provider"
    assert body["namespaces"] == [auth_dataset]
    assert body["token"].startswith("a2x_pat_")
    assert body["key_id"]
    assert body["key_prefix"].startswith("a2x_pat_")

    # A second listing must NOT contain plaintext.
    keys = client.get("/api/auth/keys", headers=admin_headers).json()
    for k in keys:
        assert "token" not in k

    # The token also doesn't appear in /principals listing.
    plist = client.get("/api/auth/principals", headers=admin_headers).json()
    serialized = str(plist)
    assert body["token"] not in serialized


def test_create_user_requires_namespaces_list(
    auth_initialized_app, admin_headers
):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/auth/principals",
        json={"handle": "bob", "role": "user"},   # namespaces missing → None
        headers=admin_headers,
    )
    assert r.status_code == 400
    assert "namespaces" in r.text.lower()


def test_create_admin_must_omit_namespaces(auth_initialized_app, admin_headers):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/auth/principals",
        json={"handle": "second_admin", "role": "admin", "namespaces": ["x"]},
        headers=admin_headers,
    )
    assert r.status_code == 400
    assert "admin" in r.text.lower()


def test_create_user_rejects_unknown_namespace(
    auth_initialized_app, admin_headers, auth_dataset
):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/auth/principals",
        json={"handle": "claire", "role": "user",
              "namespaces": [auth_dataset, "does_not_exist"]},
        headers=admin_headers,
    )
    assert r.status_code == 400
    assert "unknown namespace" in r.text.lower()


def test_create_duplicate_handle_rejected(
    auth_initialized_app, admin_headers, auth_dataset
):
    client, _ = auth_initialized_app
    payload = {"handle": "dup", "role": "user", "namespaces": [auth_dataset]}
    r1 = client.post("/api/auth/principals", json=payload, headers=admin_headers)
    assert r1.status_code == 201
    r2 = client.post("/api/auth/principals", json=payload, headers=admin_headers)
    assert r2.status_code == 400
    assert "handle" in r2.text.lower()


def test_non_admin_cannot_create_principals(
    auth_initialized_app, admin_headers, provider_headers
):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/auth/principals",
        json={"handle": "sneaky", "role": "user", "namespaces": []},
        headers=provider_headers,
    )
    assert r.status_code == 403


def test_get_principal_admin_only(auth_initialized_app, admin_headers, user_headers):
    client, _ = auth_initialized_app
    r = client.get("/api/auth/principals", headers=user_headers)
    assert r.status_code == 403
    r = client.get("/api/auth/principals", headers=admin_headers)
    assert r.status_code == 200


def test_patch_principal_add_namespace(
    auth_initialized_app, admin_headers, auth_dataset
):
    client, _ = auth_initialized_app
    # Create a provider with empty namespaces (legal but useless until granted).
    p = client.post(
        "/api/auth/principals",
        json={"handle": "claimable", "role": "provider", "namespaces": []},
        headers=admin_headers,
    ).json()
    # Grant access to auth_dataset via PATCH.
    r = client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"namespaces": [auth_dataset]},
        headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    assert r.json()["namespaces"] == [auth_dataset]


def test_patch_principal_disable(auth_initialized_app, admin_headers, auth_dataset):
    """A disabled principal still has a key, but its requests should 401."""
    client, _ = auth_initialized_app
    p = client.post(
        "/api/auth/principals",
        json={"handle": "deactme", "role": "user", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()
    target_token = p["token"]
    target_headers = {"Authorization": f"Bearer {target_token}"}
    # Works before disable.
    r = client.get("/api/auth/whoami", headers=target_headers)
    assert r.status_code == 200
    # Disable.
    r = client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"disabled": True}, headers=admin_headers,
    )
    assert r.status_code == 200
    # Now token is dead.
    r = client.get("/api/auth/whoami", headers=target_headers)
    assert r.status_code == 401
