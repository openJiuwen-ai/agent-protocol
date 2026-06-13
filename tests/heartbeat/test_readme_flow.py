"""End-to-end test mirroring client/README.md §2 closely.

Verifies the documented 3-role flow (admin → provider → user) works
against current server + SDK code, exactly as a user would copy-paste
each snippet. Any failure here means the README example is broken.
"""

from __future__ import annotations

import uuid

import pytest


@pytest.fixture
def admin_app(lite_app, tmp_path):
    """Mirror §2.1's "a2x-registry auth init" step programmatically.
    Yields (TestClient, admin_token) — equivalent to having logged in."""
    from a2x_registry.auth.store import AuthStore
    from a2x_registry.auth.deps import set_auth_store
    store, admin_token = AuthStore.bootstrap(data_dir=tmp_path / "auth_data_rm")
    set_auth_store(store)
    try:
        yield lite_app, admin_token
    finally:
        set_auth_store(None)


def _admin_call(client, admin_token, method, path, **kwargs):
    """Simulate admin._transport.request(...) with the auth header pre-set."""
    headers = kwargs.pop("headers", {}) or {}
    headers["Authorization"] = f"Bearer {admin_token}"
    return client.request(method, path, headers=headers, **kwargs)


def test_readme_admin_flow_runs_end_to_end(admin_app):
    """§2.1 admin block — bootstrap, create ns with auth+heartbeat, issue keys."""
    client, admin_token = admin_app

    # (1) create namespace with auth AND heartbeat in one request (README pattern)
    r = _admin_call(client, admin_token, "POST", "/api/datasets", json={
        "name": "translators",
        "auth_required": True,
        "lease_config": {"enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60},
    })
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["auth_required"] is True
    assert body["lease_config"]["enabled"] is True
    assert body["lease_config"]["min_ttl"] == 10
    assert body["lease_config"]["grace_period"] == 60

    # (3) issue provider token
    r = _admin_call(client, admin_token, "POST", "/api/auth/principals", json={
        "handle":     "alice-provider",
        "role":       "provider",
        "namespaces": ["translators"],
    })
    assert r.status_code == 201, r.text
    provider_token = r.json()["token"]
    assert provider_token.startswith("a2x_pat_")

    # (4) issue user token
    r = _admin_call(client, admin_token, "POST", "/api/auth/principals", json={
        "handle":     "bob-user",
        "role":       "user",
        "namespaces": ["translators"],
    })
    assert r.status_code == 201, r.text
    user_token = r.json()["token"]
    assert user_token.startswith("a2x_pat_")


def test_readme_provider_flow_runs_end_to_end(admin_app):
    """§2.2 provider block — register with auto_renew, set_status busy/online, deregister."""
    client, admin_token = admin_app

    # Setup from §2.1
    _admin_call(client, admin_token, "POST", "/api/datasets", json={
        "name": "translators", "auth_required": True,
        "lease_config": {"enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60},
    })
    r = _admin_call(client, admin_token, "POST", "/api/auth/principals", json={
        "handle": "alice-rm-p1",
        "role": "provider",
        "namespaces": ["translators"],
    })
    provider_token = r.json()["token"]

    # Provider headers (simulates cli_token.json containing this token)
    p_headers = {"Authorization": f"Bearer {provider_token}"}

    # Register with lease_ttl
    agent_card = {
        "name":               "EN-ZH Translator",
        "description":        "Translate EN → simplified ZH.",
        "version":            "1.2.0",
        "url":                "https://translator-01.internal/a2a",
        "capabilities":       {},
        "defaultInputModes":  ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [{"id": "translate", "name": "Paragraph",
                    "description": "段落翻译", "tags": ["en-zh"]}],
        "region":             "cn-east-1",
        "status":             "online",
    }
    r = client.post(
        "/api/datasets/translators/services/a2a",
        json={"agent_card": agent_card, "dataset": "translators", "lease_ttl": 60},
        headers=p_headers,
    )
    assert r.status_code == 200, r.text
    body = r.json()
    my_sid = body["service_id"]
    assert body["lease_ttl"] == 60
    assert body["lease_expires_at"] is not None

    # Update business field
    r = client.put(
        f"/api/datasets/translators/services/{my_sid}",
        json={"region": "cn-east-2"},
        headers=p_headers,
    )
    assert r.status_code == 200, r.text

    # set_status("busy")
    r = client.put(
        f"/api/datasets/translators/services/{my_sid}",
        json={"status": "busy"},
        headers=p_headers,
    )
    assert r.status_code == 200, r.text
    # Verify it's now busy
    detail = client.get(
        f"/api/datasets/translators/services/{my_sid}", headers=p_headers,
    ).json()
    md = detail.get("metadata") or {}
    assert md.get("status") == "busy"

    # set_status("online") — restore
    r = client.put(
        f"/api/datasets/translators/services/{my_sid}",
        json={"status": "online"},
        headers=p_headers,
    )
    assert r.status_code == 200

    # Deregister
    r = client.delete(
        f"/api/datasets/translators/services/{my_sid}", headers=p_headers,
    )
    assert r.status_code == 200

    # Confirm gone
    r = client.get(
        f"/api/datasets/translators/services/{my_sid}", headers=p_headers,
    )
    assert r.status_code == 404


def test_readme_user_flow_runs_end_to_end(admin_app):
    """§2.3 user block — list with status=online filter, get by id, reserve."""
    client, admin_token = admin_app

    # Setup: namespace + provider + user
    _admin_call(client, admin_token, "POST", "/api/datasets", json={
        "name": "translators", "auth_required": True,
        "lease_config": {"enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60},
    })
    provider_token = _admin_call(client, admin_token,
        "POST", "/api/auth/principals", json={
            "handle": "alice-rm-p2", "role": "provider",
            "namespaces": ["translators"]},
    ).json()["token"]
    user_token = _admin_call(client, admin_token,
        "POST", "/api/auth/principals", json={
            "handle": "bob-rm-u1", "role": "user",
            "namespaces": ["translators"]},
    ).json()["token"]

    p_headers = {"Authorization": f"Bearer {provider_token}"}
    u_headers = {"Authorization": f"Bearer {user_token}"}

    # Provider registers 3 services with different status / regions
    def reg(name, region, status, ttl=60):
        card = {
            "name": name, "description": f"agent {name}",
            "version": "1.0", "url": f"http://{name}.internal/a2a",
            "capabilities": {},
            "defaultInputModes": ["text/plain"],
            "defaultOutputModes": ["text/plain"],
            "skills": [{"id": "s", "name": "s", "description": "s", "tags": ["t"]}],
            "region": region, "status": status,
        }
        r = client.post(
            "/api/datasets/translators/services/a2a",
            json={"agent_card": card, "dataset": "translators", "lease_ttl": ttl},
            headers=p_headers,
        )
        assert r.status_code == 200, r.text
        return r.json()["service_id"]

    sid_online_cn1 = reg("t-online-cn1", "cn-east-1", "online")
    sid_busy_cn1   = reg("t-busy-cn1",   "cn-east-1", "busy")
    sid_online_cn2 = reg("t-online-cn2", "cn-east-2", "online")

    # ── User: list with status=online + region filter ──
    r = client.get(
        "/api/datasets/translators/services",
        params={"status": "online", "region": "cn-east-1"},
        headers=u_headers,
    )
    assert r.status_code == 200
    ids = [s["id"] for s in r.json()]
    # Only the online + cn-east-1 candidate should match
    assert sid_online_cn1 in ids
    assert sid_busy_cn1 not in ids     # status=busy filtered out
    assert sid_online_cn2 not in ids   # wrong region

    # ── User: get by id ──
    r = client.get(
        f"/api/datasets/translators/services/{sid_online_cn1}",
        headers=u_headers,
    )
    assert r.status_code == 200
    detail = r.json()
    md = detail.get("metadata") or {}
    assert md.get("name") == "t-online-cn1" or detail.get("name") == "t-online-cn1"
    assert md.get("region") == "cn-east-1"

    # ── User: reserve ──
    r = client.post(
        "/api/datasets/translators/reservations",
        json={
            "filters": {"region": "cn-east-1", "status": "online"},
            "n": 1, "ttl_seconds": 60,
        },
        headers=u_headers,
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert len(body["reservations"]) == 1
    assert body["reservations"][0]["id"] == sid_online_cn1
    # holder_id MUST be coerced to user's principal_id (auth-required ns rule)
    me = client.get("/api/auth/whoami", headers=u_headers).json()
    assert body["holder_id"] == me["principal_id"]


def test_readme_user_cannot_register_provider_can(admin_app):
    """Negative path: confirms role enforcement matches what 2.3 implies
    (user 只读 + 预约)."""
    client, admin_token = admin_app
    _admin_call(client, admin_token, "POST", "/api/datasets", json={
        "name": "translators", "auth_required": True,
        "lease_config": {"enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60},
    })
    user_token = _admin_call(client, admin_token,
        "POST", "/api/auth/principals", json={
            "handle": "bob-rm-u2", "role": "user",
            "namespaces": ["translators"]},
    ).json()["token"]
    u_headers = {"Authorization": f"Bearer {user_token}"}

    card = {
        "name": "user-attempts-to-register", "description": "x",
        "version": "1.0", "url": "http://x/a",
        "capabilities": {}, "defaultInputModes": ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [{"id": "s", "name": "s", "description": "s", "tags": ["t"]}],
    }
    r = client.post(
        "/api/datasets/translators/services/a2a",
        json={"agent_card": card, "dataset": "translators", "lease_ttl": 60},
        headers=u_headers,
    )
    assert r.status_code == 403  # user role cannot register


def test_readme_status_busy_hides_from_user_filter(admin_app):
    """The README §2.2 → §2.3 contract: provider's set_status("busy") makes
    the service disappear from user's status=online filter results; restoring
    to "online" brings it back. This is what makes 'self-disable' actually
    work end-to-end across the two role boundaries."""
    client, admin_token = admin_app
    _admin_call(client, admin_token, "POST", "/api/datasets", json={
        "name": "translators", "auth_required": True,
        "lease_config": {"enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60},
    })
    provider_token = _admin_call(client, admin_token,
        "POST", "/api/auth/principals", json={
            "handle": "alice-disable", "role": "provider",
            "namespaces": ["translators"]},
    ).json()["token"]
    user_token = _admin_call(client, admin_token,
        "POST", "/api/auth/principals", json={
            "handle": "bob-disable", "role": "user",
            "namespaces": ["translators"]},
    ).json()["token"]

    p_headers = {"Authorization": f"Bearer {provider_token}"}
    u_headers = {"Authorization": f"Bearer {user_token}"}

    card = {
        "name": "disable-test", "description": "x",
        "version": "1.0", "url": "http://x/a",
        "capabilities": {}, "defaultInputModes": ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [{"id": "s", "name": "s", "description": "s", "tags": ["t"]}],
        "region": "cn", "status": "online",
    }
    r = client.post(
        "/api/datasets/translators/services/a2a",
        json={"agent_card": card, "dataset": "translators", "lease_ttl": 30},
        headers=p_headers,
    )
    sid = r.json()["service_id"]

    # User sees it
    hits = client.get(
        "/api/datasets/translators/services",
        params={"status": "online"}, headers=u_headers,
    ).json()
    assert any(s["id"] == sid for s in hits)

    # Provider self-disables
    client.put(
        f"/api/datasets/translators/services/{sid}",
        json={"status": "busy"}, headers=p_headers,
    )

    # User no longer sees it (status=online filter excludes busy)
    hits = client.get(
        "/api/datasets/translators/services",
        params={"status": "online"}, headers=u_headers,
    ).json()
    assert all(s["id"] != sid for s in hits)

    # Provider restores
    client.put(
        f"/api/datasets/translators/services/{sid}",
        json={"status": "online"}, headers=p_headers,
    )

    # User sees it again
    hits = client.get(
        "/api/datasets/translators/services",
        params={"status": "online"}, headers=u_headers,
    ).json()
    assert any(s["id"] == sid for s in hits)
