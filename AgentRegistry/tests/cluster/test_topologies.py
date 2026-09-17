"""Multi-node integration on a full mesh: every node sees every node's
services; a departed node's records are evicted by HOLD on the survivors;
an evicted origin is not resurrected by anti-entropy.

All share one dataset "svc"; each node registers a uniquely-named service.
Full mesh = every pair of nodes has a direct session (no relay), so a record
reaches the whole cluster from its origin's single broadcast. ``converge``
drives anti-entropy to a fixed point as a backstop for dropped pushes.
"""

from __future__ import annotations

from itertools import combinations

from a2x_registry.cluster.config import ClusterConfig

from .helpers import (
    FakeClock, FakeRegistry, InProcessTransport, build_store, converge, visible,
)

DS = "svc"


def _node(tmp_path, transport, name, services, *, config=None, clock=None):
    reg = FakeRegistry()
    for s in services:
        reg.add_generic(DS, s)
    store = build_store(tmp_path, name, reg, transport, config=config, clock=clock)
    return store, reg


def _full_mesh(stores) -> None:
    """Connect every pair directly (one connect per pair sets up both
    directions' sessions)."""
    for a, b in combinations(stores, 2):
        a.connect_peer(b._advertise)


def _mutate(store, reg, name):
    sid = reg.add_generic(DS, name)
    store.on_local_mutation(DS, sid, "register", reg.get_entry(DS, sid))
    return sid


# ── full-mesh reachability ───────────────────────────────────────────────

def test_full_mesh_any_to_any(tmp_path):
    t = InProcessTransport()
    A, rA = _node(tmp_path, t, "A", ["a-svc"])
    B, rB = _node(tmp_path, t, "B", ["b-svc"])
    C, rC = _node(tmp_path, t, "C", ["c-svc"])
    _full_mesh([A, B, C])
    converge([A, B, C])

    everyone = {"a-svc", "b-svc", "c-svc"}
    assert visible(A, rA, DS) == everyone
    assert visible(B, rB, DS) == everyone
    assert visible(C, rC, DS) == everyone


def test_full_mesh_four_nodes(tmp_path):
    t = InProcessTransport()
    A, rA = _node(tmp_path, t, "A", ["a-svc"])
    B, rB = _node(tmp_path, t, "B", ["b-svc"])
    C, rC = _node(tmp_path, t, "C", ["c-svc"])
    D, rD = _node(tmp_path, t, "D", ["d-svc"])
    _full_mesh([A, B, C, D])
    converge([A, B, C, D])

    everyone = {"a-svc", "b-svc", "c-svc", "d-svc"}
    for s, r in [(A, rA), (B, rB), (C, rC), (D, rD)]:
        assert visible(s, r, DS) == everyone


def test_new_register_and_update_propagate(tmp_path):
    t = InProcessTransport()
    A, rA = _node(tmp_path, t, "A", ["a-svc"])
    B, rB = _node(tmp_path, t, "B", ["b-svc"])
    C, rC = _node(tmp_path, t, "C", ["c-svc"])
    _full_mesh([A, B, C])
    converge([A, B, C])

    # New registration at A broadcasts directly to every peer.
    _mutate(A, rA, "a-new")
    assert "a-new" in visible(C, rC, DS)

    # An update at C (new version of c-svc) reaches A directly.
    sid = rC.add_generic(DS, "c-svc", description="v2")
    from a2x_registry.cluster.state import make_key
    C._state.local_versions[make_key(DS, sid)] = [C._next_ts(), "C"]
    C.on_local_mutation(DS, sid, "update", rC.get_entry(DS, sid))
    a_view = {r["wrapped"]["name"]: r["wrapped"]["description"]
              for r in A.foreign_rows(DS)}
    assert a_view.get("c-svc") == "v2"


# ── departure: HOLD evicts the departed node's records ───────────────────

def _short_cfg():
    return ClusterConfig(keepalive_interval=5, hold_timeout=10)


def test_all_depart_each_keeps_only_local(tmp_path):
    """No one keepalives; advance past HOLD and sweep. Each node keeps only
    its own local service (every foreign replica evicted)."""
    clk = FakeClock()
    t = InProcessTransport()
    A, rA = _node(tmp_path, t, "A", ["a-svc"], config=_short_cfg(), clock=clk)
    B, rB = _node(tmp_path, t, "B", ["b-svc"], config=_short_cfg(), clock=clk)
    C, rC = _node(tmp_path, t, "C", ["c-svc"], config=_short_cfg(), clock=clk)
    _full_mesh([A, B, C])
    converge([A, B, C])
    assert visible(A, rA, DS) == {"a-svc", "b-svc", "c-svc"}

    clk.advance(20)
    for s in (A, B, C):
        s.check_hold()
    assert visible(A, rA, DS) == {"a-svc"}
    assert visible(B, rB, DS) == {"b-svc"}
    assert visible(C, rC, DS) == {"c-svc"}


def test_only_departed_origin_evicted_others_stay(tmp_path):
    """A,B,C full mesh; C departs but B keeps keepaliving. A evicts only C."""
    clk = FakeClock()
    t = InProcessTransport()
    A, rA = _node(tmp_path, t, "A", ["a-svc"], config=_short_cfg(), clock=clk)
    B, rB = _node(tmp_path, t, "B", ["b-svc"], config=_short_cfg(), clock=clk)
    C, rC = _node(tmp_path, t, "C", ["c-svc"], config=_short_cfg(), clock=clk)
    _full_mesh([A, B, C])
    converge([A, B, C])
    assert visible(A, rA, DS) == {"a-svc", "b-svc", "c-svc"}

    # t=8: B keepalives (refreshes its HOLD on A); C stays silent.
    clk.advance(8)
    B.emit_keepalive()

    # t=12: C silent 12s > hold_timeout=10 → evicted; B silent only 4s → kept.
    clk.advance(4)
    A.check_hold()
    assert "c-svc" not in visible(A, rA, DS)
    assert "b-svc" in visible(A, rA, DS)


def test_evicted_origin_not_resurrected_by_anti_entropy(tmp_path):
    """After A evicts C via HOLD, anti-entropy with B (who hasn't evicted yet)
    must NOT re-pull C's records — the suppression cooldown blocks it."""
    clk = FakeClock()
    t = InProcessTransport()
    A, rA = _node(tmp_path, t, "A", ["a-svc"], config=_short_cfg(), clock=clk)
    B, rB = _node(tmp_path, t, "B", ["b-svc"], config=_short_cfg(), clock=clk)
    C, rC = _node(tmp_path, t, "C", ["c-svc"], config=_short_cfg(), clock=clk)
    _full_mesh([A, B, C])
    converge([A, B, C])
    assert "c-svc" in visible(A, rA, DS)

    # t=8: B keepalives (stays alive on A); C is silent.
    clk.advance(8)
    B.emit_keepalive()

    # t=12: A evicts C, keeps B. B has NOT swept → still holds c-svc.
    clk.advance(4)
    A.check_hold()
    assert "c-svc" not in visible(A, rA, DS)
    assert "b-svc" in visible(A, rA, DS)

    # A reconciles B (still holds c-svc) → suppression rejects the re-pull.
    A.reconcile(A._sessions["B"])
    assert "c-svc" not in visible(A, rA, DS)

    # Once B also evicts C, the whole cluster is free of it.
    B.check_hold()
    converge([A, B], rounds=2)
    assert "c-svc" not in visible(A, rA, DS)
    assert "c-svc" not in visible(B, rB, DS)
