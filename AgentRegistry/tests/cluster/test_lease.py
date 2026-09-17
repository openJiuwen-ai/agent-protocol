"""Unit tests for the shared generic LeaseTable state machine.

Drives the table with explicit ``now`` values so the HEALTHY → UNHEALTHY →
hard-delete transitions are deterministic and millisecond-fast (no sleep).
This is the core both heartbeat and cluster-liveness reuse.
"""

from __future__ import annotations

import pytest

from a2x_registry.common.lease import Lease, LeaseState, LeaseTable


def test_install_healthy_then_expire_then_delete():
    t = LeaseTable()
    t.install("k", ttl=10, grace=5, now=100.0)

    lease = t.get("k")
    assert lease is not None
    assert lease.state == LeaseState.HEALTHY
    assert lease.expires_at == 110.0
    assert lease.grace_deadline == 115.0

    # Before expiry: no transition.
    nu, dele = t.sweep_tick(now=109.0)
    assert nu == [] and dele == []
    assert t.get("k").state == LeaseState.HEALTHY

    # At/after expiry: HEALTHY → UNHEALTHY.
    nu, dele = t.sweep_tick(now=110.0)
    assert nu == ["k"] and dele == []
    assert t.get("k").state == LeaseState.UNHEALTHY

    # After grace deadline: UNHEALTHY → hard-delete (removed from table).
    nu, dele = t.sweep_tick(now=115.0)
    assert dele == ["k"]
    assert t.get("k") is None


def test_renew_restores_and_extends():
    t = LeaseTable()
    t.install("k", ttl=10, grace=5, now=100.0)
    t.sweep_tick(now=110.0)  # → UNHEALTHY
    assert t.get("k").state == LeaseState.UNHEALTHY

    # Renew within grace window recovers HEALTHY and pushes the deadlines.
    lease = t.renew("k", now=112.0)
    assert lease.state == LeaseState.HEALTHY
    assert lease.expires_at == 122.0
    assert lease.grace_deadline == 127.0
    assert lease.last_renew_at == 112.0


def test_renew_missing_raises_keyerror():
    t = LeaseTable()
    with pytest.raises(KeyError):
        t.renew("nope")


def test_revoke_soft_marks_unhealthy_idempotent():
    t = LeaseTable()
    t.install("k", ttl=10, grace=5, now=100.0)
    assert t.revoke("k", now=103.0) is True
    lease = t.get("k")
    assert lease.state == LeaseState.UNHEALTHY
    assert lease.expires_at == 103.0
    assert lease.grace_deadline == 108.0
    # Idempotent on a missing key.
    assert t.revoke("missing") is False


def test_revoke_permanent_removes():
    t = LeaseTable()
    t.install("k", ttl=10, grace=5, now=100.0)
    assert t.revoke("k", permanent=True) is True
    assert t.get("k") is None


def test_install_expired_seeds_grace_window():
    t = LeaseTable()
    t.install("k", ttl=10, grace=5, now=100.0, expired=True)
    lease = t.get("k")
    assert lease.state == LeaseState.UNHEALTHY
    assert lease.expires_at == 100.0
    assert lease.grace_deadline == 105.0
    # Still recoverable within grace.
    t.renew("k", now=104.0)
    assert t.get("k").state == LeaseState.HEALTHY


def test_is_unhealthy_and_items_and_drop():
    t = LeaseTable()
    t.install("a", ttl=10, grace=5, now=100.0)
    t.install("b", ttl=10, grace=5, now=100.0)
    assert t.is_unhealthy("a") is False
    assert t.is_unhealthy("missing") is False
    t.revoke("a", now=101.0)
    assert t.is_unhealthy("a") is True

    keys = {k for k, _ in t.items()}
    assert keys == {"a", "b"}

    t.drop("a")
    assert t.get("a") is None
    t.drop("a")  # idempotent


def test_same_tick_expire_and_delete_when_past_grace():
    """A lease whose grace_deadline is already in the past flips UNHEALTHY
    and is deleted in the same sweep pass."""
    t = LeaseTable()
    t.install("k", ttl=10, grace=5, now=100.0)  # expires 110, grace 115
    nu, dele = t.sweep_tick(now=200.0)
    assert nu == ["k"] and dele == ["k"]
    assert t.get("k") is None
