"""Admin-facing client methods: ``create_dataset`` with auth + lease_config
inline, and ``create_principal`` for issuing API keys.

Tests use ``httpx.MockTransport`` to capture the outgoing request body and
verify the SDK builds it correctly; no live server involved.
"""

from __future__ import annotations

import json
from typing import Any

import httpx
import pytest

from a2x_registry_client import (
    A2XRegistryClient,
    DatasetCreateResponse,
    PrincipalCreateResponse,
)
from a2x_registry_client.errors import ValidationError
from a2x_registry_client.transport import HTTPTransport


def _client_with_handler(handler) -> A2XRegistryClient:
    """Build a real client whose transport is wired to an httpx MockTransport."""
    mock = httpx.MockTransport(handler)
    client = A2XRegistryClient(
        base_url="http://test/", api_key="a2x_pat_admin", ownership_file=False,
    )
    client._transport._client.close()
    client._transport._client = httpx.Client(
        base_url="http://test/", transport=mock,
        headers={"Authorization": "Bearer a2x_pat_admin"},
    )
    return client


# ─── create_dataset ──────────────────────────────────────────────────────────


def test_create_dataset_legacy_body_byte_equal():
    """Calling create_dataset(name) without new flags produces the same
    body as before: name + embedding_model + default formats; NO auth_required
    key and NO lease_config key."""
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        captured["path"] = req.url.path
        captured["method"] = req.method
        return httpx.Response(200, json={
            "dataset": "ds1", "embedding_model": "all-MiniLM-L6-v2",
            "formats": {"a2a": "v0.0"}, "status": "created",
        })

    client = _client_with_handler(handler)
    resp = client.create_dataset("ds1")
    assert isinstance(resp, DatasetCreateResponse)
    assert resp.dataset == "ds1"

    assert captured["method"] == "POST"
    assert captured["path"] == "/api/datasets"
    body = captured["body"]
    assert body["name"] == "ds1"
    assert "auth_required" not in body, body
    assert "lease_config" not in body, body


def test_create_dataset_with_auth_required_only():
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        return httpx.Response(200, json={
            "dataset": "secure", "embedding_model": "all-MiniLM-L6-v2",
            "formats": {"a2a": "v0.0"}, "status": "created",
            "auth_required": True,
        })

    client = _client_with_handler(handler)
    client.create_dataset("secure", auth_required=True)
    assert captured["body"]["auth_required"] is True
    assert "lease_config" not in captured["body"]


def test_create_dataset_with_inline_lease_config():
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        return httpx.Response(200, json={
            "dataset": "hb", "embedding_model": "all-MiniLM-L6-v2",
            "formats": {"a2a": "v0.0"}, "status": "created",
            "lease_config": {"enabled": True, "min_ttl": 10,
                             "max_ttl": 600, "grace_period": 60},
        })

    client = _client_with_handler(handler)
    client.create_dataset("hb", lease_config={
        "enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60,
    })
    cfg = captured["body"]["lease_config"]
    assert cfg["enabled"] is True
    assert cfg["min_ttl"] == 10
    assert cfg["grace_period"] == 60


def test_create_dataset_with_both_auth_and_lease():
    """One-shot: admin gets auth + heartbeat in a single create_dataset."""
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        return httpx.Response(200, json={
            "dataset": "translators", "embedding_model": "all-MiniLM-L6-v2",
            "formats": {"a2a": "v0.0"}, "status": "created",
            "auth_required": True,
            "lease_config": {"enabled": True, "min_ttl": 10,
                             "max_ttl": 600, "grace_period": 60},
        })

    client = _client_with_handler(handler)
    client.create_dataset(
        "translators", auth_required=True,
        lease_config={"enabled": True, "min_ttl": 10,
                      "max_ttl": 600, "grace_period": 60},
    )
    assert captured["body"]["auth_required"] is True
    assert captured["body"]["lease_config"]["enabled"] is True


# ─── create_principal ───────────────────────────────────────────────────────


def test_create_principal_provider_role():
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        captured["path"] = req.url.path
        return httpx.Response(201, json={
            "principal_id": "u_abc123",
            "handle": "alice",
            "role": "provider",
            "namespaces": ["translators"],
            "key_id": "k_xyz789",
            "key_prefix": "a2x_pat_yfVE",
            "token": "a2x_pat_yfVEn1qRbHfeomIHxwAoa74ImKh_Y286OhEWORcRC1U",
        })

    client = _client_with_handler(handler)
    resp = client.create_principal(
        "alice", "provider", namespaces=["translators"],
    )
    assert isinstance(resp, PrincipalCreateResponse)
    assert resp.principal_id == "u_abc123"
    assert resp.role == "provider"
    assert resp.namespaces == ["translators"]
    assert resp.token.startswith("a2x_pat_")

    # Request body
    assert captured["path"] == "/api/auth/principals"
    body = captured["body"]
    assert body["handle"] == "alice"
    assert body["role"] == "provider"
    assert body["namespaces"] == ["translators"]
    assert "note" not in body  # omitted when None


def test_create_principal_admin_role_no_namespaces():
    """Admin role can omit ``namespaces`` (None → global scope)."""
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        return httpx.Response(201, json={
            "principal_id": "u_root2", "handle": "root2", "role": "admin",
            "namespaces": None, "key_id": "k_a", "key_prefix": "a2x_pat_aaaa",
            "token": "a2x_pat_aaaaaaaa",
        })

    client = _client_with_handler(handler)
    resp = client.create_principal("root2", "admin")
    assert resp.namespaces is None
    assert "namespaces" not in captured["body"]


def test_create_principal_with_note():
    captured: dict[str, Any] = {}

    def handler(req: httpx.Request) -> httpx.Response:
        captured["body"] = json.loads(req.content.decode())
        return httpx.Response(201, json={
            "principal_id": "u_x", "handle": "ops", "role": "user",
            "namespaces": ["ds1"], "key_id": "k_x",
            "key_prefix": "a2x_pat_xxxx", "token": "a2x_pat_xxxxxxxx",
        })

    client = _client_with_handler(handler)
    client.create_principal("ops", "user", ["ds1"], note="ticket-1234")
    assert captured["body"]["note"] == "ticket-1234"


def test_create_principal_propagates_validation_error():
    """Backend 400 (e.g. unknown namespace, dup handle) surfaces as
    ``ValidationError`` — same code path as any other 400."""
    def handler(req: httpx.Request) -> httpx.Response:
        return httpx.Response(400, json={"detail": "handle 'dup' already in use"})

    client = _client_with_handler(handler)
    with pytest.raises(ValidationError):
        client.create_principal("dup", "user", ["ds1"])
