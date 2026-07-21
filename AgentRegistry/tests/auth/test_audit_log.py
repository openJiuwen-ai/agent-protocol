"""Audit log: required events appear; plaintext token NEVER does."""

from __future__ import annotations

import json
import re


def _read_audit(client_pair) -> list[dict]:
    """Convenience: yield parsed JSONL entries from the live store's audit log.

    Lazy import: ``lite_app`` wipes ``a2x_registry.*`` from ``sys.modules``
    and re-imports after monkeypatching, so any top-level import of
    ``a2x_registry.auth.deps`` would bind to the WRONG (pre-wipe) module
    instance — whose ``_store`` was never set by the fixture. Re-import
    here to bind to the freshly-loaded module.
    """
    from a2x_registry.auth.deps import get_auth_store
    store = get_auth_store()
    assert store is not None, "auth_initialized_app didn't inject a store"
    path = store.data_dir / "audit.log"
    if not path.exists():
        return []
    out = []
    for line in path.read_text(encoding="utf-8").strip().splitlines():
        if line:
            out.append(json.loads(line))
    return out


def test_principal_and_key_created_events(auth_initialized_app, admin_headers, auth_dataset):
    client, _ = auth_initialized_app
    client.post(
        "/api/auth/principals",
        json={"handle": "audit-target", "role": "user", "namespaces": [auth_dataset]},
        headers=admin_headers,
    )
    events = [e["event"] for e in _read_audit(auth_initialized_app)]
    assert "principal.created" in events
    # At least 2 key.created: one for bootstrap + one for this principal.
    assert events.count("key.created") >= 2


def test_auth_failed_event_on_bad_token(auth_initialized_app, auth_dataset):
    client, _ = auth_initialized_app
    client.get(
        f"/api/datasets/{auth_dataset}/services",
        headers={"Authorization": "Bearer a2x_pat_obviously_bogus_value"},
    )
    audit = _read_audit(auth_initialized_app)
    fails = [e for e in audit if e.get("event") == "auth.failed"]
    assert fails, "no auth.failed event recorded"
    assert any(e.get("reason") in ("invalid_token", "wrong_prefix") for e in fails)


def test_permission_denied_event_on_cross_namespace(
    auth_initialized_app, auth_dataset, provider_token_other_ns,
):
    client, _ = auth_initialized_app
    token_other, _ = provider_token_other_ns
    headers_other = {"Authorization": f"Bearer {token_other}"}
    client.get(f"/api/datasets/{auth_dataset}/services", headers=headers_other)
    audit = _read_audit(auth_initialized_app)
    denies = [e for e in audit if e.get("event") == "permission.denied"]
    assert any(e.get("reason") == "namespace_out_of_scope" for e in denies)


def test_audit_log_never_contains_plaintext_tokens(
    auth_initialized_app, admin_headers, auth_dataset
):
    """Defense-in-depth: scan the whole audit.log for the a2x_pat_ regex."""
    client, _ = auth_initialized_app
    # Generate as much traffic as possible (creates, failed auths, role denials).
    client.post(
        "/api/auth/principals",
        json={"handle": "scan-target", "role": "user", "namespaces": [auth_dataset]},
        headers=admin_headers,
    )
    client.get(
        f"/api/datasets/{auth_dataset}/services",
        headers={"Authorization": "Bearer a2x_pat_definitely_not_real_abcdefghij"},
    )
    from a2x_registry.auth.deps import get_auth_store
    store = get_auth_store()
    audit = (store.data_dir / "audit.log").read_text(encoding="utf-8")
    # 40+ chars after the prefix == matches a real token. 12 chars == key_prefix
    # which we DO log (intentionally). Use a length threshold to distinguish.
    matches = re.findall(r"a2x_pat_[A-Za-z0-9_-]{20,}", audit)
    assert not matches, f"audit.log leaks token-like material: {matches!r}"
