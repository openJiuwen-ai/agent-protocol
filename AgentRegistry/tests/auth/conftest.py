"""Shared fixtures for the auth test suite.

Builds on the per-feature ``tests/conftest.py`` fixtures (``lite_app``,
``agent_card``) by adding a few auth-specific layers:

- ``auth_initialized_app`` — same TestClient as ``lite_app``, but with a
  bootstrapped ``AuthStore`` injected. The bootstrap admin's plaintext
  token is returned for the role-matrix tests to use.
- ``auth_dataset`` / ``anon_dataset`` — fresh datasets per test in each
  flavor, deleted on teardown. Tests can use both side-by-side to verify
  per-namespace gating.
- ``provider_token`` / ``user_token`` / ``*_other_ns`` — provision
  principals bound to a specific dataset (or a different dataset, for
  cross-namespace tests) and yield the freshly-issued plaintext token.

Each fixture is function-scoped to inherit ``lite_app``'s tmpdir + module
reload, so tests cannot leak state into each other.
"""

from __future__ import annotations

import uuid
from typing import Iterator, Tuple

import pytest

from fastapi.testclient import TestClient


# ── Auth bootstrap ───────────────────────────────────────────────────────


@pytest.fixture
def auth_initialized_app(lite_app: TestClient, tmp_path) -> Iterator[Tuple[TestClient, str]]:
    """Bootstrap the auth module on top of the lite_app's tmpdir, then
    yield (client, plaintext_admin_token).

    Uses an isolated ``auth_data`` directory under ``tmp_path`` (NOT the
    package-bundled location) so the test never touches the developer's
    own auth data. The deps singleton is reset to None on teardown so
    subsequent tests get a fresh anonymous registry.
    """
    from a2x_registry.auth.store import AuthStore
    from a2x_registry.auth.deps import set_auth_store, get_auth_store

    auth_data = tmp_path / "auth_data"
    store, token = AuthStore.bootstrap(data_dir=auth_data)
    set_auth_store(store)
    try:
        yield lite_app, token
    finally:
        set_auth_store(None)


@pytest.fixture
def admin_headers(auth_initialized_app) -> dict:
    _client, token = auth_initialized_app
    return {"Authorization": f"Bearer {token}"}


# ── Dataset fixtures (per-test fresh) ────────────────────────────────────


def _new_ds_name(prefix: str = "auth") -> str:
    return f"{prefix}_" + uuid.uuid4().hex[:8]


@pytest.fixture
def auth_dataset(auth_initialized_app, admin_headers) -> Iterator[str]:
    """Create an auth_required=True dataset using the admin token.

    Yields the dataset name. Teardown deletes via admin so the next test
    starts clean. The dataset has all-default formats / embedding.
    """
    client, _ = auth_initialized_app
    name = _new_ds_name("authds")
    r = client.post(
        "/api/datasets",
        json={"name": name, "auth_required": True},
        headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    assert r.json()["auth_required"] is True
    yield name
    r = client.delete(f"/api/datasets/{name}", headers=admin_headers)
    assert r.status_code == 200, r.text


@pytest.fixture
def anon_dataset(auth_initialized_app, admin_headers) -> Iterator[str]:
    """Create an auth_required=False dataset (no token sent at creation).

    Used in tests that verify auth-required and anon namespaces coexist on
    the same auth-initialized registry — the anon namespace should keep
    behaving exactly like a legacy zero-auth dataset.

    Teardown deletes with admin headers because some tests toggle the
    namespace's auth-config to ``required=True`` mid-test; the cleanup
    must succeed either way.
    """
    client, _ = auth_initialized_app
    name = _new_ds_name("anonds")
    r = client.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    assert r.json()["auth_required"] is False
    yield name
    r = client.delete(f"/api/datasets/{name}", headers=admin_headers)
    assert r.status_code == 200, r.text


# ── Principal / token factories ──────────────────────────────────────────


def _provision(client: TestClient, admin_headers: dict, role: str,
               namespaces: list, handle: str | None = None) -> Tuple[str, str, str]:
    """Internal helper: POST /api/auth/principals, return (principal_id, token, handle)."""
    handle = handle or f"{role}_{uuid.uuid4().hex[:6]}"
    body = {"handle": handle, "role": role, "namespaces": namespaces}
    r = client.post("/api/auth/principals", json=body, headers=admin_headers)
    assert r.status_code == 201, r.text
    data = r.json()
    return data["principal_id"], data["token"], handle


@pytest.fixture
def provider_token(auth_initialized_app, admin_headers, auth_dataset) -> str:
    """Provision a provider principal bound to ``auth_dataset``. Returns plaintext token."""
    client, _ = auth_initialized_app
    _, token, _ = _provision(client, admin_headers, "provider", [auth_dataset])
    return token


@pytest.fixture
def provider_headers(provider_token) -> dict:
    return {"Authorization": f"Bearer {provider_token}"}


@pytest.fixture
def user_token(auth_initialized_app, admin_headers, auth_dataset) -> str:
    """Provision a user principal bound to ``auth_dataset``. Returns plaintext token."""
    client, _ = auth_initialized_app
    _, token, _ = _provision(client, admin_headers, "user", [auth_dataset])
    return token


@pytest.fixture
def user_headers(user_token) -> dict:
    return {"Authorization": f"Bearer {user_token}"}


@pytest.fixture
def provider_token_other_ns(auth_initialized_app, admin_headers) -> Tuple[str, str]:
    """Provision a provider principal bound to a *different* dataset.

    Yields ``(token, other_dataset_name)``. Used for cross-namespace tests:
    this provider should be able to mutate inside ``other_dataset_name``
    but NOT inside ``auth_dataset``.
    """
    client, _ = auth_initialized_app
    other = _new_ds_name("otherds")
    r = client.post(
        "/api/datasets",
        json={"name": other, "auth_required": True},
        headers=admin_headers,
    )
    assert r.status_code == 200, r.text
    _, token, _ = _provision(client, admin_headers, "provider", [other])
    yield token, other
    client.delete(f"/api/datasets/{other}", headers=admin_headers)


# ── A2A card factory (mirrors the parent conftest one) ───────────────────


@pytest.fixture
def a2a_card_factory():
    """Build a minimal A2A v1.0 card. The parent conftest defines this too,
    but we duplicate here so this module is self-contained for clarity."""
    def _make(name: str = "agent-1") -> dict:
        return {
            "name": name,
            "description": "tester",
            "url": "http://example.invalid",
            "version": "1.0",
            "protocolVersion": "0.0.1",
            "capabilities": {},
            "defaultInputModes": ["text/plain"],
            "defaultOutputModes": ["text/plain"],
            "skills": [
                {"id": "s", "name": "s", "description": "s", "tags": ["t"]}
            ],
        }
    return _make
