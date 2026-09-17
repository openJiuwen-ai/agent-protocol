"""Full-mesh liveness: direct-link keepalive / HOLD is the sole eviction path.

In a full mesh every member is a direct peer, so a node's liveness is just
its keepalive: a peer silent past ``hold_timeout`` is dropped, its replicated
records evicted, and a suppression cooldown armed so anti-entropy can't
resurrect them from a peer that hasn't evicted yet.
"""

from __future__ import annotations

from a2x_registry.cluster.config import ClusterConfig

from .helpers import FakeClock, FakeRegistry, InProcessTransport, build_store


def _short_cfg():
    # Small windows so HOLD can be driven with an explicit clock.
    return ClusterConfig(keepalive_interval=5, hold_timeout=10)


def test_hold_drops_silent_peer_and_evicts_its_records(tmp_path):
    t = InProcessTransport()
    clk = FakeClock()
    rA, rB = FakeRegistry(), FakeRegistry()
    rB.add_generic("ds", "b-svc")
    A = build_store(tmp_path, "A", rA, t, config=_short_cfg(), clock=clk)
    build_store(tmp_path, "B", rB, t, config=_short_cfg())
    A.connect_peer("B")  # A pulls B's record
    assert A.state_summary()["foreign_records"] == 1
    assert "B" in {p["node_id"] for p in A.state_summary()["peers"]}

    # B goes silent past the HOLD window → dropped, its records evicted.
    clk.advance(20)
    dropped = A.check_hold()
    assert dropped == ["B"]
    assert A.state_summary()["foreign_records"] == 0
    assert A.state_summary()["peers"] == []


def test_keepalive_refreshes_hold(tmp_path):
    t = InProcessTransport()
    clk = FakeClock()
    rA = FakeRegistry()
    rA.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t, config=_short_cfg(), clock=clk)
    build_store(tmp_path, "B", FakeRegistry(), t, config=_short_cfg())
    A.connect_peer("B")

    clk.advance(8)
    A.handle_keepalive("B")  # B still alive → refreshes last_seen
    assert A.check_hold() == []  # 8s < hold_timeout from the refreshed mark
    clk.advance(8)
    assert A.check_hold() == []  # 8s since refresh, still alive
    clk.advance(20)
    assert A.check_hold() == ["B"]  # finally silent past HOLD


def test_hold_eviction_arms_suppression_no_resurrection(tmp_path):
    """A–B–C full mesh. C's record reaches both A and B. C dies; A's HOLD
    for C expires and evicts it. Anti-entropy with B (who hasn't evicted C
    yet) must NOT resurrect C's record — the suppression cooldown blocks it."""
    t = InProcessTransport()
    clk = FakeClock()
    rA, rB, rC = FakeRegistry(), FakeRegistry(), FakeRegistry()
    rC.add_generic("ds", "c-svc")
    A = build_store(tmp_path, "A", rA, t, config=_short_cfg(), clock=clk)
    B = build_store(tmp_path, "B", rB, t, config=_short_cfg())
    C = build_store(tmp_path, "C", rC, t, config=_short_cfg())
    # Full mesh: every pair connected.
    A.connect_peer("B")
    A.connect_peer("C")
    B.connect_peer("C")
    assert any(r["origin_id"] == "C" for r in A.foreign_wrapped("ds"))
    assert any(r["origin_id"] == "C" for r in B.foreign_wrapped("ds"))

    # t=8: B keepalives (stays alive on A); C is silent (departed).
    clk.advance(8)
    B.emit_keepalive()

    # t=12: A's HOLD for C expires → A evicts C + arms suppression; B kept.
    clk.advance(4)
    assert "C" in A.check_hold()
    assert all(r["origin_id"] != "C" for r in A.foreign_wrapped("ds"))

    # A reconciles with B, who still serves C's record. Suppression rejects it.
    A.reconcile(A._sessions["B"])
    assert all(r["origin_id"] != "C" for r in A.foreign_wrapped("ds"))


def test_reconnect_lifts_suppression(tmp_path):
    """After a peer is HOLD-evicted (suppressed), reconnecting it clears the
    cooldown so its records sync again."""
    t = InProcessTransport()
    clk = FakeClock()
    rA, rB = FakeRegistry(), FakeRegistry()
    rB.add_generic("ds", "b-svc")
    A = build_store(tmp_path, "A", rA, t, config=_short_cfg(), clock=clk)
    build_store(tmp_path, "B", rB, t, config=_short_cfg())
    A.connect_peer("B")
    clk.advance(20)
    A.check_hold()
    assert A.state_summary()["foreign_records"] == 0

    # B comes back: reconnect lifts suppression and re-pulls its record.
    A.connect_peer("B")
    assert A.state_summary()["foreign_records"] == 1


def test_prune_suppression_clears_expired_entries(tmp_path):
    t = InProcessTransport()
    clk = FakeClock()
    rA, rB = FakeRegistry(), FakeRegistry()
    rB.add_generic("ds", "b-svc")
    A = build_store(tmp_path, "A", rA, t, config=_short_cfg(), clock=clk)
    build_store(tmp_path, "B", rB, t, config=_short_cfg())
    A.connect_peer("B")
    clk.advance(20)
    A.check_hold()
    assert A._evicted_until  # cooldown armed

    # Past the retention window the prune drops the inert entry.
    clk.advance(A.config.tombstone_retention + 1)
    A.prune_suppression()
    assert not A._evicted_until
