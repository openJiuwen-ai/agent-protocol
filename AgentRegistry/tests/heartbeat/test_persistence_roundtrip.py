"""Persisted lease_ttl + restart recovery."""

from __future__ import annotations

import json
from pathlib import Path


def test_lease_ttl_persisted_to_api_config(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory,
):
    """Register with lease_ttl → api_config.json contains the value, so
    a restart can know "this service expects heartbeats"."""
    card = a2a_card_factory("persist")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 30},
    )
    sid = r.json()["service_id"]

    from a2x_registry.common.paths import database_dir
    p = Path(database_dir()) / heartbeat_enabled_dataset / "api_config.json"
    data = json.loads(p.read_text(encoding="utf-8"))
    target = next(s for s in data["services"] if s.get("service_id") == sid)
    assert target.get("lease_ttl") == 30


def test_restart_recovery_grants_grace_lease(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """Simulate restart recovery: drop the in-memory lease, then call
    recover_from_persisted; the lease should be re-installed in UNHEALTHY
    state with grace_deadline set so the client gets one chance to
    re-establish before hard-delete."""
    card = a2a_card_factory("recov")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 30},
    )
    sid = r.json()["service_id"]

    # Drop the lease (simulates restart wiping in-memory state).
    heartbeat_store.drop(heartbeat_enabled_dataset, sid)
    assert heartbeat_store.get_lease(heartbeat_enabled_dataset, sid) is None

    # Recover from persisted lease_ttl (would happen at backend startup).
    heartbeat_store.recover_from_persisted([(heartbeat_enabled_dataset, sid, 30)])
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    assert lease is not None
    # Recovered leases start UNHEALTHY — they need one heartbeat to be HEALTHY.
    from a2x_registry.heartbeat.models import HBState
    assert lease.state == HBState.UNHEALTHY
    # Grace deadline is in the future so we don't immediately hard-delete.
    import time
    assert lease.grace_deadline > time.monotonic()


def test_recovered_lease_recovers_on_heartbeat(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """The whole point of recovery: a client that heartbeats within grace
    after restart gets HEALTHY without re-registering."""
    card = a2a_card_factory("recovheal")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 30},
    )
    sid = r.json()["service_id"]

    heartbeat_store.drop(heartbeat_enabled_dataset, sid)
    heartbeat_store.recover_from_persisted([(heartbeat_enabled_dataset, sid, 30)])

    # Client heartbeats — must succeed and return HEALTHY
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/{sid}/heartbeat", json={},
    )
    assert r.status_code == 200
    assert r.json()["state"] == "healthy"
    assert heartbeat_store.is_unhealthy(heartbeat_enabled_dataset, sid) is False
