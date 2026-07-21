"""HeartbeatStore.sweep_tick — drive state machine directly without time."""

from __future__ import annotations

import time


def test_sweep_no_leases_is_noop(heartbeat_store):
    """Sweeper on an empty registry returns empty lists — no errors."""
    nu, td = heartbeat_store.sweep_tick(now=time.monotonic())
    assert nu == []
    assert td == []


def test_sweep_healthy_lease_remains_healthy(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """Lease that hasn't expired stays HEALTHY through a sweep tick."""
    card = a2a_card_factory("h")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 60},
    )
    sid = r.json()["service_id"]
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    # Tick well before expiry
    nu, td = heartbeat_store.sweep_tick(now=lease.expires_at - 1.0)
    assert (heartbeat_enabled_dataset, sid) not in nu
    assert (heartbeat_enabled_dataset, sid) not in td


def test_sweep_state_progression(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """Drive a lease HEALTHY → UNHEALTHY → HARD-DELETE via direct sweep_tick.

    No real sleep — pass synthetic ``now`` to each call.
    """
    card = a2a_card_factory("p")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 10},
    )
    sid = r.json()["service_id"]
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    key = (heartbeat_enabled_dataset, sid)

    # tick 1: just past expires_at but well before grace_deadline → UNHEALTHY
    nu, td = heartbeat_store.sweep_tick(now=lease.expires_at + 0.01)
    assert key in nu
    assert key not in td

    # tick 2: same time, already UNHEALTHY → no transition
    nu, td = heartbeat_store.sweep_tick(now=lease.expires_at + 0.02)
    assert key not in nu  # not "newly" unhealthy
    assert key not in td

    # tick 3: past grace_deadline → hard delete signal
    nu, td = heartbeat_store.sweep_tick(now=lease.grace_deadline + 0.001)
    assert key in td
    # Lease is now removed from the store; next sweep finds nothing.
    nu2, td2 = heartbeat_store.sweep_tick(now=lease.grace_deadline + 0.002)
    assert (nu2, td2) == ([], [])


def test_sweeper_daemon_thread_actually_runs(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """End-to-end with the production sweeper daemon thread (period=5s).

    We can't speed up the production daemon, so we use the same
    sweep_once entry point that the daemon calls. This verifies the
    sweeper → registry.deregister chain works.
    """
    from a2x_registry.heartbeat.sweeper import HeartbeatSweeper
    from a2x_registry.backend.routers.dataset import get_registry_service

    svc = get_registry_service()
    sweeper = HeartbeatSweeper(svc, heartbeat_store, period=60.0)  # don't start daemon

    card = a2a_card_factory("dn")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 10},
    )
    sid = r.json()["service_id"]
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)

    # Push the lease past grace_deadline by mutating in place — fast forward.
    lease.expires_at = time.monotonic() - 0.01
    lease.grace_deadline = time.monotonic() - 0.005

    sweeper.sweep_once()

    # Entry should be hard-deleted now.
    listed = lite_app.get(
        f"/api/datasets/{heartbeat_enabled_dataset}/services?include_unhealthy=true",
    ).json()
    assert all(s["id"] != sid for s in listed)


def test_sweep_survives_broken_deregister(
    lite_app, heartbeat_enabled_dataset, a2a_card_factory, heartbeat_store,
):
    """If RegistryService.deregister raises, the sweeper logs and moves on
    — must not crash the daemon thread."""
    from a2x_registry.heartbeat.sweeper import HeartbeatSweeper

    class _BrokenSvc:
        def deregister(self, *a, **kw):
            raise RuntimeError("oops")

    sweeper = HeartbeatSweeper(_BrokenSvc(), heartbeat_store, period=60.0)
    # Install a lease and force it to be due for hard delete
    card = a2a_card_factory("broke")
    r = lite_app.post(
        f"/api/datasets/{heartbeat_enabled_dataset}/services/a2a",
        json={"agent_card": card, "dataset": heartbeat_enabled_dataset, "lease_ttl": 10},
    )
    sid = r.json()["service_id"]
    lease = heartbeat_store.get_lease(heartbeat_enabled_dataset, sid)
    lease.grace_deadline = time.monotonic() - 0.001

    # sweep_once must NOT raise
    sweeper.sweep_once()
