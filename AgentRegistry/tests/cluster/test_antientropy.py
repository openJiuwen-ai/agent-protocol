"""Anti-entropy heals deltas that a dropped push missed."""

from __future__ import annotations

from a2x_registry.cluster.sweepers import AntiEntropySweeper

from .helpers import FakeRegistry, InProcessTransport, build_store


def test_reconcile_recovers_dropped_push(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")

    # Partition A→B, then mutate: the push is dropped silently (best-effort).
    t.cut("A", "B")
    sid = rA.add_generic("ds", "missed")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))
    assert all(r["name"] != "missed" for r in B.foreign_wrapped("ds"))

    # Heal and let anti-entropy reconcile → the missed delta converges.
    t.dropped.clear()
    A.reconcile(A._sessions["B"])
    assert any(r["name"] == "missed" for r in B.foreign_wrapped("ds"))


def test_sweeper_tick_reconciles_and_gcs(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")

    t.cut("A", "B")
    sid = rA.add_generic("ds", "missed")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))
    t.dropped.clear()

    # One sweeper tick performs the reconcile (and GC) for us.
    AntiEntropySweeper(A, period=999).tick()
    assert any(r["name"] == "missed" for r in B.foreign_wrapped("ds"))


def test_sweeper_tick_survives_unreachable_peer(tmp_path):
    t = InProcessTransport()
    rA = FakeRegistry()
    rA.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    A.connect_peer("B")
    t.cut("A", "B")  # B unreachable
    # Tick must not raise even though reconcile fails.
    AntiEntropySweeper(A, period=999).tick()
