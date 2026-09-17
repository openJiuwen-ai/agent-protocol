"""All the failure modes that produce 401: missing, malformed, invalid, revoked."""

from __future__ import annotations


def test_missing_auth_header_yields_401(auth_initialized_app, auth_dataset):
    client, _ = auth_initialized_app
    r = client.get(f"/api/datasets/{auth_dataset}/services")
    assert r.status_code == 401
    assert "WWW-Authenticate" in r.headers


def test_wrong_scheme_yields_401(auth_initialized_app, auth_dataset):
    client, _ = auth_initialized_app
    # Basic scheme instead of Bearer
    r = client.get(
        f"/api/datasets/{auth_dataset}/services",
        headers={"Authorization": "Basic abcd"},
    )
    assert r.status_code == 401


def test_wrong_token_prefix_yields_401(auth_initialized_app, auth_dataset):
    client, _ = auth_initialized_app
    r = client.get(
        f"/api/datasets/{auth_dataset}/services",
        headers={"Authorization": "Bearer ghp_github_style_token_value"},
    )
    assert r.status_code == 401


def test_unknown_token_yields_401(auth_initialized_app, auth_dataset):
    client, _ = auth_initialized_app
    r = client.get(
        f"/api/datasets/{auth_dataset}/services",
        headers={"Authorization": "Bearer a2x_pat_definitely_not_a_real_key_at_all"},
    )
    assert r.status_code == 401


def test_revoked_token_yields_401(
    auth_initialized_app, admin_headers, provider_headers,
):
    client, _ = auth_initialized_app
    # Create a fresh key for the provider, then revoke it.
    new = client.post("/api/auth/keys", json={"name": "soon-revoked"},
                      headers=provider_headers).json()
    new_h = {"Authorization": f"Bearer {new['token']}"}
    # Works first.
    r = client.get("/api/auth/whoami", headers=new_h)
    assert r.status_code == 200
    # Revoke via admin.
    client.delete(f"/api/auth/keys/{new['key_id']}", headers=admin_headers)
    # Now 401.
    r = client.get("/api/auth/whoami", headers=new_h)
    assert r.status_code == 401


def test_auth_endpoints_404_before_bootstrap(lite_app):
    """When the registry has never been auth-initialized, the entire
    /api/auth/* tree returns 404. Test runs on a plain ``lite_app``
    (no auth_initialized_app) to exercise the pre-bootstrap state."""
    for path in ("/api/auth/whoami", "/api/auth/principals", "/api/auth/keys"):
        r = lite_app.get(path)
        assert r.status_code == 404, (path, r.text)
        assert "auth init" in r.text.lower() or "not initialized" in r.text.lower()
