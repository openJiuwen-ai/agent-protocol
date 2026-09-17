"""POST/GET/DELETE /api/auth/keys — own keys + admin-sees-all."""

from __future__ import annotations


def test_principal_can_create_own_key(auth_initialized_app, provider_headers):
    client, _ = auth_initialized_app
    r = client.post(
        "/api/auth/keys",
        json={"name": "second-laptop"},
        headers=provider_headers,
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["token"].startswith("a2x_pat_")
    assert body["name"] == "second-laptop"
    # The new token authenticates as the same principal.
    headers2 = {"Authorization": f"Bearer {body['token']}"}
    who = client.get("/api/auth/whoami", headers=headers2).json()
    me = client.get("/api/auth/whoami", headers=provider_headers).json()
    assert who["principal_id"] == me["principal_id"]


def test_list_keys_returns_only_own_for_non_admin(
    auth_initialized_app, admin_headers, provider_headers, user_headers
):
    client, _ = auth_initialized_app
    # Each principal has at least one key (the initial one).
    p_keys = client.get("/api/auth/keys", headers=provider_headers).json()
    u_keys = client.get("/api/auth/keys", headers=user_headers).json()
    all_keys = client.get("/api/auth/keys", headers=admin_headers).json()

    # Non-admin's view is a subset of admin's view AND only contains its own.
    p_principal = client.get("/api/auth/whoami", headers=provider_headers).json()["principal_id"]
    u_principal = client.get("/api/auth/whoami", headers=user_headers).json()["principal_id"]
    assert all(k["principal_id"] == p_principal for k in p_keys)
    assert all(k["principal_id"] == u_principal for k in u_keys)
    # admin sees both.
    pids = {k["principal_id"] for k in all_keys}
    assert {p_principal, u_principal}.issubset(pids)


def test_non_admin_principal_id_query_is_silently_ignored(
    auth_initialized_app, admin_headers, provider_headers, user_headers
):
    """Non-admins can't info-leak by querying other principals' keys."""
    client, _ = auth_initialized_app
    u_principal = client.get("/api/auth/whoami", headers=user_headers).json()["principal_id"]
    # Provider tries to query user's keys via ?principal_id=
    r = client.get(
        "/api/auth/keys",
        params={"principal_id": u_principal},
        headers=provider_headers,
    )
    assert r.status_code == 200
    p_principal = client.get("/api/auth/whoami", headers=provider_headers).json()["principal_id"]
    # Returned keys still belong to provider, not the queried user.
    assert all(k["principal_id"] == p_principal for k in r.json())


def test_revoke_own_key(auth_initialized_app, provider_headers):
    client, _ = auth_initialized_app
    # Create a fresh key so we don't kill the bootstrap key.
    new = client.post("/api/auth/keys", json={"name": "to-revoke"}, headers=provider_headers).json()
    new_headers = {"Authorization": f"Bearer {new['token']}"}
    r = client.delete(f"/api/auth/keys/{new['key_id']}", headers=provider_headers)
    assert r.status_code == 200
    # Revoked token is dead.
    r = client.get("/api/auth/whoami", headers=new_headers)
    assert r.status_code == 401


def test_non_admin_cannot_revoke_others_keys(
    auth_initialized_app, provider_headers, user_headers
):
    client, _ = auth_initialized_app
    # User creates a key; provider tries to revoke it.
    u_key = client.post("/api/auth/keys", json={"name": "victim"}, headers=user_headers).json()
    r = client.delete(f"/api/auth/keys/{u_key['key_id']}", headers=provider_headers)
    assert r.status_code == 403


def test_admin_can_revoke_anyones_key(
    auth_initialized_app, admin_headers, provider_headers
):
    client, _ = auth_initialized_app
    new = client.post("/api/auth/keys", json={"name": "by-admin"}, headers=provider_headers).json()
    new_headers = {"Authorization": f"Bearer {new['token']}"}
    r = client.delete(f"/api/auth/keys/{new['key_id']}", headers=admin_headers)
    assert r.status_code == 200
    # Confirm dead.
    r = client.get("/api/auth/whoami", headers=new_headers)
    assert r.status_code == 401
