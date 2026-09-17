"""Anon and auth-required namespaces must coexist on the same registry.

This is the central backward-compat invariant: even AFTER an admin runs
``auth init``, datasets created with ``auth_required=false`` (or without
the field at all) keep working with zero authentication, identical to a
pre-bootstrap registry. Existing pip-installed clients keep working.
"""

from __future__ import annotations


def test_anon_dataset_remains_open_for_register(
    auth_initialized_app, anon_dataset, agent_card,
):
    """Anyone can register an A2A on the anon namespace — no headers."""
    client, _ = auth_initialized_app
    card = agent_card("anon-write")
    r = client.post(
        f"/api/datasets/{anon_dataset}/services/a2a",
        json={"agent_card": card, "dataset": anon_dataset, "persistent": True},
    )
    assert r.status_code == 200, r.text


def test_anon_dataset_remains_open_for_read(
    auth_initialized_app, anon_dataset,
):
    """Reads on anon dataset require no token even with auth initialized."""
    client, _ = auth_initialized_app
    r = client.get(f"/api/datasets/{anon_dataset}/services")
    assert r.status_code == 200


def test_anon_dataset_owner_id_stays_null(
    auth_initialized_app, anon_dataset, agent_card,
):
    """Even on an auth-initialized registry, registering anonymously on an
    anon namespace must NOT stamp an owner_id (we don't know who they are)."""
    client, _ = auth_initialized_app
    card = agent_card("anon-no-owner")
    r = client.post(
        f"/api/datasets/{anon_dataset}/services/a2a",
        json={"agent_card": card, "dataset": anon_dataset, "persistent": True},
    )
    assert r.status_code == 200
    sid = r.json()["service_id"]

    detail = client.get(
        f"/api/datasets/{anon_dataset}/services/{sid}"
    ).json()
    md = detail.get("metadata") or {}
    assert md.get("owner_id") in (None, "")
    assert detail.get("owner_id") in (None, "")


def test_anon_reservation_holder_id_is_NOT_coerced(
    auth_initialized_app, anon_dataset, agent_card,
):
    """On anon namespaces the legacy behavior of accepting body holder_id
    is preserved — without it, existing client SDK reservation flows that
    set a custom holder_id would break silently."""
    client, _ = auth_initialized_app
    card = agent_card("anon-reservable")
    card["status"] = "online"
    client.post(
        f"/api/datasets/{anon_dataset}/services/a2a",
        json={"agent_card": card, "dataset": anon_dataset, "persistent": True},
    )
    r = client.post(
        f"/api/datasets/{anon_dataset}/reservations",
        json={"filters": {}, "n": 1, "ttl_seconds": 30,
              "holder_id": "my_custom_holder"},
    )
    assert r.status_code == 200, r.text
    assert r.json()["holder_id"] == "my_custom_holder"


def test_auth_required_dataset_unaffected_by_anon_traffic(
    auth_initialized_app, auth_dataset,
):
    """Anon requests to auth_dataset get 401, NEVER accidentally succeed
    just because some anon namespace exists in the same registry."""
    client, _ = auth_initialized_app
    r = client.get(f"/api/datasets/{auth_dataset}/services")
    assert r.status_code == 401
