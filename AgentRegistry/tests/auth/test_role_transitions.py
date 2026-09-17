"""Role transitions via PATCH /api/auth/principals/{id}.

Covers the invariants AuthStore.update_principal enforces (which the
existing principal_crud tests only partially exercise):

  - admin role MUST have namespaces=None
  - provider/user role MUST have namespaces as a list
  - Crossing roles requires passing both fields together if the old
    role's namespace shape is incompatible with the new role's.
"""

from __future__ import annotations


def test_promote_provider_to_admin_requires_namespaces_none(
    auth_initialized_app, admin_headers, auth_dataset
):
    """Provider with namespaces=[ds] cannot be PATCHed to admin without
    also clearing namespaces — invariant says admin↔namespaces=None."""
    client, _ = auth_initialized_app
    p = client.post(
        "/api/auth/principals",
        json={"handle": "to_promote", "role": "provider", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()

    # Just changing role → should fail because namespaces would still be [auth_dataset].
    r = client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"role": "admin"}, headers=admin_headers,
    )
    assert r.status_code == 400, r.text
    assert "admin" in r.text.lower() and "namespaces" in r.text.lower()


def test_promote_provider_to_admin_with_namespaces_none(
    auth_initialized_app, admin_headers, auth_dataset
):
    """Both fields set in the same PATCH → succeeds, principal becomes global admin."""
    client, _ = auth_initialized_app
    p = client.post(
        "/api/auth/principals",
        json={"handle": "becoming_admin", "role": "provider", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()

    r = client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"role": "admin", "namespaces": None}, headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    updated = r.json()
    assert updated["role"] == "admin"
    assert updated["namespaces"] is None

    # And their token now has admin powers everywhere.
    target_headers = {"Authorization": f"Bearer {p['token']}"}
    r = client.get("/api/auth/principals", headers=target_headers)
    assert r.status_code == 200


def test_demote_admin_to_user_requires_explicit_namespaces(
    auth_initialized_app, admin_headers
):
    """admin (namespaces=None) → user MUST provide a namespaces list."""
    client, _ = auth_initialized_app
    # Create a second admin so demoting doesn't lock us out.
    p = client.post(
        "/api/auth/principals",
        json={"handle": "second_admin", "role": "admin"},
        headers=admin_headers,
    ).json()

    # Just demoting to user → must fail (no namespaces list).
    r = client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"role": "user"}, headers=admin_headers,
    )
    assert r.status_code == 400, r.text


def test_re_enable_disabled_principal(
    auth_initialized_app, admin_headers, auth_dataset
):
    """disabled=False clears disabled_at and re-grants access."""
    client, _ = auth_initialized_app
    p = client.post(
        "/api/auth/principals",
        json={"handle": "yo_yo", "role": "user", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()
    h = {"Authorization": f"Bearer {p['token']}"}

    client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"disabled": True}, headers=admin_headers,
    )
    assert client.get("/api/auth/whoami", headers=h).status_code == 401

    r = client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"disabled": False}, headers=admin_headers,
    )
    assert r.status_code == 200
    assert r.json()["disabled_at"] is None
    assert client.get("/api/auth/whoami", headers=h).status_code == 200


def test_patch_principal_audit_event(
    auth_initialized_app, admin_headers, auth_dataset
):
    """principal.updated must land in audit.log so role changes are traceable."""
    client, _ = auth_initialized_app
    p = client.post(
        "/api/auth/principals",
        json={"handle": "audit_me", "role": "provider", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()
    client.patch(
        f"/api/auth/principals/{p['principal_id']}",
        json={"namespaces": []}, headers=admin_headers,
    )

    from a2x_registry.auth.deps import get_auth_store
    import json as _json
    audit = (get_auth_store().data_dir / "audit.log").read_text(encoding="utf-8")
    events = [_json.loads(l) for l in audit.strip().splitlines() if l]
    updates = [e for e in events if e["event"] == "principal.updated"]
    assert any(e.get("principal_id") == p["principal_id"] for e in updates)
