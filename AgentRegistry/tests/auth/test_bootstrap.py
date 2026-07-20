"""AuthStore.bootstrap behavior + idempotency + token format invariants."""

from __future__ import annotations

import json
import re

import pytest

from a2x_registry.auth.store import AuthStore
from a2x_registry.auth.tokens import TOKEN_PREFIX


def test_bootstrap_creates_files_and_returns_admin_token(tmp_path):
    data_dir = tmp_path / "auth_data"
    store, token = AuthStore.bootstrap(data_dir=data_dir)

    # Token format invariants — every issuance must look like ``a2x_pat_<43>``.
    assert token.startswith(TOKEN_PREFIX)
    assert len(token) - len(TOKEN_PREFIX) >= 40  # token_urlsafe(32) → 43 chars

    # Files exist + are JSON dicts the loader can read.
    p_file = data_dir / "principals.json"
    k_file = data_dir / "api_keys.json"
    assert p_file.exists()
    assert k_file.exists()
    principals = json.loads(p_file.read_text(encoding="utf-8"))
    keys = json.loads(k_file.read_text(encoding="utf-8"))
    assert len(principals) == 1
    assert principals[0]["role"] == "admin"
    assert principals[0]["namespaces"] is None
    assert len(keys) == 1
    assert keys[0]["principal_id"] == principals[0]["id"]


def test_bootstrap_does_not_persist_plaintext(tmp_path):
    """Defense-in-depth: the plaintext token must NEVER appear on disk."""
    data_dir = tmp_path / "auth_data"
    _, token = AuthStore.bootstrap(data_dir=data_dir)

    for child in data_dir.iterdir():
        content = child.read_text(encoding="utf-8", errors="ignore")
        assert token not in content, f"plaintext token leaked into {child.name}"


def test_bootstrap_refuses_second_time(tmp_path):
    data_dir = tmp_path / "auth_data"
    AuthStore.bootstrap(data_dir=data_dir)
    with pytest.raises(FileExistsError):
        AuthStore.bootstrap(data_dir=data_dir)


def test_bootstrap_with_explicit_token(tmp_path):
    """CI / scripted bootstrap can pass in the token; the store stores its hash."""
    data_dir = tmp_path / "auth_data"
    target = TOKEN_PREFIX + "x" * 43
    store, returned = AuthStore.bootstrap(data_dir=data_dir, admin_token=target)
    assert returned == target
    ctx = store.authenticate(target)
    assert ctx.is_admin


def test_bootstrap_rejects_bad_token_prefix(tmp_path):
    """Passing a non-a2x_pat_ token to --admin-token must hard-fail."""
    with pytest.raises(ValueError):
        AuthStore.bootstrap(data_dir=tmp_path / "auth_data", admin_token="not_a_pat_token")


def test_load_or_none_returns_none_pre_bootstrap(tmp_path):
    """Helps the FastAPI startup detect 'not yet bootstrapped' without raising."""
    assert AuthStore.load_or_none(data_dir=tmp_path / "auth_data") is None


def test_load_or_none_round_trips_after_bootstrap(tmp_path):
    data_dir = tmp_path / "auth_data"
    store1, token = AuthStore.bootstrap(data_dir=data_dir)
    store2 = AuthStore.load_or_none(data_dir=data_dir)
    assert store2 is not None
    # Both instances should authenticate the same token to the same identity.
    ctx1 = store1.authenticate(token)
    ctx2 = store2.authenticate(token)
    assert ctx1.principal_id == ctx2.principal_id
    assert ctx1.role == ctx2.role == "admin"


def test_audit_log_emits_creation_events(tmp_path):
    data_dir = tmp_path / "auth_data"
    AuthStore.bootstrap(data_dir=data_dir)
    audit = (data_dir / "audit.log").read_text(encoding="utf-8").strip().splitlines()
    events = [json.loads(line)["event"] for line in audit]
    assert "principal.created" in events
    assert "key.created" in events


def test_audit_log_never_contains_plaintext_token(tmp_path):
    data_dir = tmp_path / "auth_data"
    _, token = AuthStore.bootstrap(data_dir=data_dir)
    log = (data_dir / "audit.log").read_text(encoding="utf-8")
    # Catch both verbatim leaks and the 43-char body alone (paranoia).
    assert token not in log
    body = token[len(TOKEN_PREFIX):]
    assert body not in log
    # And the regex used by external scanners shouldn't catch any line.
    assert re.search(r"a2x_pat_[A-Za-z0-9_-]{40,}", log) is None
