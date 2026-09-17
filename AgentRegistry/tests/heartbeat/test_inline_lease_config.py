"""POST /api/datasets {lease_config: {...}} — one-shot create + heartbeat config.

Symmetric with ``auth_required`` already being inline on create. Saves a
round-trip vs. the two-call pattern (create then POST /lease-config).
"""

from __future__ import annotations

import json
from pathlib import Path

import uuid


def _new_ds_name(prefix="oneshot"):
    return f"{prefix}_" + uuid.uuid4().hex[:8]


def test_create_with_inline_lease_config_persists(lite_app):
    """The combined request writes lease_config.json on disk identical to
    what the two-step pattern would produce."""
    name = _new_ds_name()
    r = lite_app.post("/api/datasets", json={
        "name": name,
        "lease_config": {
            "enabled": True,
            "min_ttl": 20,
            "max_ttl": 1200,
            "grace_period": 90,
        },
    })
    assert r.status_code == 200, r.text
    body = r.json()
    # Response surfaces the persisted lease config
    assert body["dataset"] == name
    assert body["lease_config"]["enabled"] is True
    assert body["lease_config"]["min_ttl"] == 20
    assert body["lease_config"]["max_ttl"] == 1200
    assert body["lease_config"]["grace_period"] == 90

    # Disk content matches
    from a2x_registry.common.paths import database_dir
    p = Path(database_dir()) / name / "lease_config.json"
    on_disk = json.loads(p.read_text(encoding="utf-8"))
    assert on_disk["enabled"] is True
    assert on_disk["min_ttl"] == 20

    # Get endpoint sees the same state
    r = lite_app.get(f"/api/datasets/{name}/lease-config")
    assert r.json()["enabled"] is True
    assert r.json()["min_ttl"] == 20


def test_create_without_lease_config_omits_key_in_response(lite_app):
    """Legacy callers don't see a new ``lease_config`` key in the response
    when they don't pass it — byte-equal output preserved."""
    name = _new_ds_name("legacy")
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200
    body = r.json()
    assert "lease_config" not in body
    assert body["status"] == "created"

    # And lease_config.json does NOT exist on disk
    from a2x_registry.common.paths import database_dir
    p = Path(database_dir()) / name / "lease_config.json"
    assert not p.exists()


def test_create_with_lease_config_enables_heartbeat_register(lite_app, a2a_card_factory):
    """The whole point: after one POST /datasets, the namespace immediately
    accepts ``lease_ttl`` on register without any further configuration."""
    name = _new_ds_name("ready")
    r = lite_app.post("/api/datasets", json={
        "name": name,
        "lease_config": {"enabled": True, "min_ttl": 5, "max_ttl": 120, "grace_period": 30},
    })
    assert r.status_code == 200

    # Register a service immediately — must succeed with lease info
    card = a2a_card_factory("ready1")
    r = lite_app.post(
        f"/api/datasets/{name}/services/a2a",
        json={"agent_card": card, "dataset": name, "lease_ttl": 30},
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["lease_ttl"] == 30
    assert body["lease_expires_at"] is not None


def test_create_with_invalid_lease_bounds_rejected(lite_app):
    """Bound validation in the inline path is identical to the standalone
    /lease-config endpoint — both go through RegistryStore.write_lease_config."""
    name = _new_ds_name("badbounds")
    # min_ttl > max_ttl → 400
    r = lite_app.post("/api/datasets", json={
        "name": name,
        "lease_config": {"enabled": True, "min_ttl": 200, "max_ttl": 50, "grace_period": 30},
    })
    assert r.status_code == 400, r.text
    # The dataset directory should still have been created (auth was
    # already written; only lease_config write failed). Either behavior
    # is defensible — current implementation creates first, so cleanup
    # is the operator's job. Test just confirms the request fails loudly.


def test_create_with_lease_config_enabled_false(lite_app):
    """``enabled: false`` is a valid configuration choice (admin staging
    the config but not turning it on yet) — writes the file, no effect on
    register matrix."""
    name = _new_ds_name("staged")
    r = lite_app.post("/api/datasets", json={
        "name": name,
        "lease_config": {"enabled": False, "min_ttl": 5, "max_ttl": 120, "grace_period": 30},
    })
    assert r.status_code == 200, r.text
    assert r.json()["lease_config"]["enabled"] is False

    # Heartbeat still not effectively enabled — passing lease_ttl on
    # register must 400 just like a namespace with no lease_config at all.
    card = {
        "name": "x", "description": "x", "version": "1.0", "url": "http://x/a",
        "capabilities": {},
        "defaultInputModes": ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [{"id": "s", "name": "s", "description": "s", "tags": ["t"]}],
    }
    r = lite_app.post(
        f"/api/datasets/{name}/services/a2a",
        json={"agent_card": card, "dataset": name, "lease_ttl": 30},
    )
    assert r.status_code == 400
    assert r.json()["detail"]["code"] == "heartbeat_not_supported"


def test_combined_auth_and_lease_in_single_request(lite_app, tmp_path):
    """admin one-shot: auth_required + lease_config in same POST /datasets."""
    # Need a bootstrapped auth store for auth_required=True
    from a2x_registry.auth.store import AuthStore
    from a2x_registry.auth.deps import set_auth_store

    store, admin_token = AuthStore.bootstrap(data_dir=tmp_path / "auth_data_oneshot")
    set_auth_store(store)
    try:
        admin_headers = {"Authorization": f"Bearer {admin_token}"}
        name = _new_ds_name("both")
        r = lite_app.post(
            "/api/datasets",
            json={
                "name": name,
                "auth_required": True,
                "lease_config": {
                    "enabled": True,
                    "min_ttl": 10,
                    "max_ttl": 600,
                    "grace_period": 60,
                },
            },
            headers=admin_headers,
        )
        assert r.status_code == 200, r.text
        body = r.json()
        assert body["auth_required"] is True
        assert body["lease_config"]["enabled"] is True
        assert body["lease_config"]["min_ttl"] == 10

        # Confirm both configs are queryable via their own endpoints
        r = lite_app.get(f"/api/datasets/{name}/auth-config")
        assert r.json()["required"] is True
        r = lite_app.get(f"/api/datasets/{name}/lease-config")
        assert r.json()["enabled"] is True
    finally:
        set_auth_store(None)


def test_inline_lease_config_pre_existing_endpoint_still_works(lite_app):
    """The standalone POST /{ds}/lease-config endpoint is still available
    — used for retrofitting heartbeat on a namespace created without it,
    or for changing bounds later."""
    # Create without lease_config
    name = _new_ds_name("retrofit")
    lite_app.post("/api/datasets", json={"name": name})

    # Retrofit via the standalone endpoint
    r = lite_app.post(
        f"/api/datasets/{name}/lease-config",
        json={"enabled": True, "min_ttl": 5, "max_ttl": 100, "grace_period": 20},
    )
    assert r.status_code == 200
    assert r.json()["enabled"] is True
