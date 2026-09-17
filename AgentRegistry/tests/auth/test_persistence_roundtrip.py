"""Disk-state round-trip invariants.

(1) Anon namespace `api_config.json` MUST omit `owner_id` (byte-equal with
    pre-auth output — the backward-compat lockfile relied on by every
    upgrade scenario).
(2) Auth-required namespace `api_config.json` MUST include `owner_id` for
    every entry registered with a caller.
(3) Revoking a key removes its hash from the in-memory by-hash index so a
    subsequent authenticate() lookup is fast-fail (defense in depth).
"""

from __future__ import annotations

import json


def test_anon_namespace_api_config_has_no_owner_id(
    auth_initialized_app, anon_dataset, agent_card
):
    """Register on an anon namespace from auth-initialized server.
    Persisted api_config.json must NOT contain owner_id."""
    client, _ = auth_initialized_app
    card = agent_card("anon-roundtrip")
    r = client.post(
        f"/api/datasets/{anon_dataset}/services/a2a",
        json={"agent_card": card, "dataset": anon_dataset, "persistent": True},
    )
    assert r.status_code == 200, r.text

    # Inspect on-disk api_config.json directly.
    from a2x_registry.common.paths import database_dir
    api_file = database_dir() / anon_dataset / "api_config.json"
    payload = json.loads(api_file.read_text(encoding="utf-8"))
    for svc in payload.get("services", []):
        assert "owner_id" not in svc, (
            f"anon namespace api_config.json must not contain owner_id; "
            f"found in entry {svc.get('service_id')!r}"
        )


def test_auth_required_namespace_api_config_records_owner(
    auth_initialized_app, auth_dataset, provider_headers, agent_card
):
    """Register on an auth-required namespace; api_config.json MUST persist
    owner_id (the provider's principal_id)."""
    client, _ = auth_initialized_app
    card = agent_card("auth-roundtrip")
    r = client.post(
        f"/api/datasets/{auth_dataset}/services/a2a",
        json={"agent_card": card, "dataset": auth_dataset, "persistent": True},
        headers=provider_headers,
    )
    assert r.status_code == 200, r.text
    sid = r.json()["service_id"]

    me = client.get("/api/auth/whoami", headers=provider_headers).json()
    expected_owner = me["principal_id"]

    from a2x_registry.common.paths import database_dir
    api_file = database_dir() / auth_dataset / "api_config.json"
    payload = json.loads(api_file.read_text(encoding="utf-8"))
    entries = {s["service_id"]: s for s in payload.get("services", [])}
    assert sid in entries, f"sid {sid!r} not in api_config.json"
    assert entries[sid]["owner_id"] == expected_owner


def test_revoked_key_removed_from_index(auth_initialized_app, provider_headers, admin_headers):
    """After revoke, the in-memory by-hash index drops the hash — so
    authenticate() misses fast even before the JSON re-read on next restart."""
    client, _ = auth_initialized_app
    # Create a fresh key so we don't kill the bootstrap one.
    new = client.post("/api/auth/keys", json={"name": "tombstone"}, headers=provider_headers).json()
    new_h = {"Authorization": f"Bearer {new['token']}"}

    # Before revoke: authenticates.
    assert client.get("/api/auth/whoami", headers=new_h).status_code == 200

    # Revoke (via admin so we cover the cross-principal path too).
    client.delete(f"/api/auth/keys/{new['key_id']}", headers=admin_headers)

    # Check the index state directly.
    from a2x_registry.auth.deps import get_auth_store
    from a2x_registry.auth.tokens import hash_token
    store = get_auth_store()
    h = hash_token(new["token"])
    assert h not in store._keys_by_hash, (
        "revoked key's hash must be removed from the by-hash index"
    )

    # And the next authenticate() correctly 401s (no leak through stale entries).
    assert client.get("/api/auth/whoami", headers=new_h).status_code == 401


def test_principals_and_keys_files_survive_restart(
    auth_initialized_app, admin_headers, auth_dataset
):
    """A fresh AuthStore.load_or_none() built from the same data_dir
    must round-trip every principal + key exactly."""
    client, _ = auth_initialized_app
    # Create some state.
    p = client.post(
        "/api/auth/principals",
        json={"handle": "restart_target", "role": "user", "namespaces": [auth_dataset]},
        headers=admin_headers,
    ).json()
    target_token = p["token"]
    target_headers = {"Authorization": f"Bearer {target_token}"}

    # "Restart": construct a fresh store from the same dir.
    from a2x_registry.auth.deps import get_auth_store
    from a2x_registry.auth.store import AuthStore
    data_dir = get_auth_store().data_dir
    fresh = AuthStore.load_or_none(data_dir=data_dir)
    assert fresh is not None
    # The target principal & key are recovered.
    p2 = fresh.get_principal(p["principal_id"])
    assert p2 is not None
    assert p2.handle == "restart_target"
    # And the original plaintext token still authenticates against the fresh store.
    ctx = fresh.authenticate(target_token)
    assert ctx.principal_id == p["principal_id"]
