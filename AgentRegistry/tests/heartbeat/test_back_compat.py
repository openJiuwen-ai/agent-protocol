"""Backward-compat lockfile: heartbeat module presence must NOT change
legacy registry behavior. Namespaces without lease_config never gate."""

from __future__ import annotations

import json
from pathlib import Path


def test_legacy_dataset_register_works_without_ttl(
    lite_app, heartbeat_disabled_dataset, a2a_card_factory,
):
    """Same shape as pre-heartbeat: register without lease_ttl → 200,
    no lease info, no persistent lease_ttl field."""
    card = a2a_card_factory("legacy")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_disabled_dataset},
    )
    assert r.status_code == 200
    body = r.json()
    assert body.get("lease_ttl") in (None, 0)
    assert body.get("lease_expires_at") in (None, 0)


def test_legacy_dataset_api_config_omits_lease_ttl(
    lite_app, heartbeat_disabled_dataset, a2a_card_factory,
):
    """Persisted api_config.json must NOT contain ``lease_ttl`` for entries
    registered without one — preserves byte-equal output with pre-heartbeat."""
    card = a2a_card_factory("noleak")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_disabled_dataset},
    )
    sid = r.json()["service_id"]

    # Read the api_config.json directly off disk.
    from a2x_registry.common.paths import database_dir
    p = Path(database_dir()) / heartbeat_disabled_dataset / "api_config.json"
    data = json.loads(p.read_text(encoding="utf-8"))
    target = next(s for s in data["services"] if s.get("service_id") == sid)
    assert "lease_ttl" not in target, (
        f"legacy register on disabled namespace must omit lease_ttl from api_config.json; "
        f"found in entry: {target}"
    )


def test_existing_anonymous_endpoints_unchanged_by_heartbeat_module(
    lite_app, heartbeat_disabled_dataset, a2a_card_factory,
):
    """Register / list / deregister on a disabled-heartbeat dataset must
    NOT require any new headers or fields beyond pre-heartbeat baseline."""
    card = a2a_card_factory("baseline")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_disabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_disabled_dataset},
    )
    assert r.status_code == 200
    sid = r.json()["service_id"]

    r = lite_app.get(f"/api/datasets/{heartbeat_disabled_dataset}/services")
    assert r.status_code == 200
    assert any(s["id"] == sid for s in r.json())

    r = lite_app.delete(f"/api/datasets/{heartbeat_disabled_dataset}/services/{sid}")
    assert r.status_code == 200
