"""Declarative membership control plane: set add/remove, bootstrap, leave
old cluster, LWW convergence, restart-rejoin, isolation, and auth.

Uses the in-process multi-store harness. ``settle`` drives the reconcile
loop (connect/disconnect + record/membership deltas) to a fixed point.
"""

from __future__ import annotations

from a2x_registry.cluster.state import ClusterState
from a2x_registry.cluster.membership import MembershipStore, MembershipRecord

from .helpers import (
    FakeRegistry, InProcessTransport, build_store, converge, settle, visible,
)


class _Ctx:
    def __init__(self, is_admin=False, namespaces=None):
        self.is_admin = is_admin
        self.namespaces = namespaces


class _Auth:
    def __init__(self, tokens):
        self._tokens = tokens

    def authenticate(self, token):
        if token in self._tokens:
            return self._tokens[token]
        raise ValueError("bad token")


def _ids(show: dict) -> set:
    return {m["node_id"] for m in show["roster"]}


def test_set_add_bootstrap(tmp_path):
    """A brand-new member learns the cluster only via the imperative join
    push (it had no session before), then forms a full mesh."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    assert B.list_peers() == []  # not connected yet

    res = A.membership.set_add([{"address": "B"}])
    assert res["cluster_id"].startswith("clu-")
    assert all(r["ok"] for r in res["results"])

    # Both adopted the same cluster, rosters agree, mesh formed both ways.
    assert A.membership.cluster_id == B.membership.cluster_id
    assert _ids(A.membership.show()) == {"A", "B"} == _ids(B.membership.show())
    assert "B" in {p.node_id for p in A.list_peers()}
    assert "A" in {p.node_id for p in B.list_peers()}


def test_set_add_three_full_mesh_and_service_visible(tmp_path):
    t = InProcessTransport()
    rA, rB, rC = FakeRegistry(), FakeRegistry(), FakeRegistry()
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    C = build_store(tmp_path, "C", rC, t)

    A.membership.set_add([{"address": "B"}, {"address": "C"}])
    settle([A, B, C])

    # All three in one cluster, every pair connected (full mesh).
    cid = A.membership.cluster_id
    for s in (A, B, C):
        assert s.membership.cluster_id == cid
        assert _ids(s.membership.show()) == {"A", "B", "C"}
    assert {p.node_id for p in A.list_peers()} == {"B", "C"}
    assert {p.node_id for p in B.list_peers()} == {"A", "C"}
    assert {p.node_id for p in C.list_peers()} == {"A", "B"}

    # A service registered on C is visible on A (direct broadcast, full mesh).
    sid = rC.add_generic("ds", "c-svc")
    C.on_local_mutation("ds", sid, "register", rC.get_entry("ds", sid))
    settle([A, B, C])
    assert "c-svc" in visible(A, rA, "ds")


def test_membership_lww_concurrent_converges(tmp_path):
    """Two adds for the same node converge to one cluster_id via LWW."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    M = build_store(tmp_path, "M", FakeRegistry(), t)

    A.membership.set_add([{"address": "M"}])  # M joins A's cluster
    B.membership.set_add([{"address": "M"}])  # then B pulls M into B's cluster
    settle([A, B, M])

    # M ends in exactly one cluster; whichever add had the higher version wins.
    assert M.membership.cluster_id in (A.membership.cluster_id, B.membership.cluster_id)
    # M's own record is the single authority for its membership.
    assert M.membership.cluster_id == B.membership.cluster_id  # B added M last


def test_set_remove_tombstone_propagates(tmp_path):
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    C = build_store(tmp_path, "C", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}, {"address": "C"}])
    settle([A, B, C])

    A.membership.set_remove([{"node_id": "C"}])
    settle([A, B, C])

    # C is gone from every roster, deterministically (no HOLD advance).
    assert "C" not in _ids(A.membership.show())
    assert "C" not in _ids(B.membership.show())
    assert "C" not in {p.node_id for p in A.list_peers()}
    assert "C" not in {p.node_id for p in B.list_peers()}
    # C reverted to standalone.
    assert C.membership.cluster_id is None


def test_leave_old_cluster_is_immediate(tmp_path):
    """When a node is pulled into a new cluster it actively leaves the old
    one (old members drop it now, not via HOLD)."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)   # cluster 1
    B = build_store(tmp_path, "B", FakeRegistry(), t)   # cluster 1 member
    X = build_store(tmp_path, "X", FakeRegistry(), t)   # cluster 2
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    cid1 = A.membership.cluster_id

    # X pulls B into cluster 2 → B leaves cluster 1.
    X.membership.set_add([{"address": "B"}])
    settle([A, B, X])

    assert B.membership.cluster_id == X.membership.cluster_id != cid1
    # A dropped B immediately (graceful leave), A is alone again.
    assert "B" not in _ids(A.membership.show())
    assert "B" not in {p.node_id for p in A.list_peers()}
    # B is now meshed with X.
    assert "X" in {p.node_id for p in B.list_peers()}


def test_restart_rejoin_from_persisted_state(tmp_path):
    """A node reloads cluster_id + last_roster from disk and auto-reconnects."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    cid = A.membership.cluster_id

    # "Restart" A: rebuild store from the same state file, fresh membership.
    state = ClusterState.load(path=tmp_path / "A.json")
    assert state.cluster_id == cid
    assert {m["node_id"] for m in state.last_roster} == {"B"}
    A2 = ClusterStoreFromState(state, t)
    A2.membership = MembershipStore(A2)
    t.register("A", A2)  # re-register the address → replaces old A
    assert A2.membership.cluster_id == cid
    assert A2.list_peers() == []           # sessions are memory-only, lost
    A2.membership.reconcile_connections()  # first sweeper tick
    assert "B" in {p.node_id for p in A2.list_peers()}  # auto-reconnected


def test_forward_compat_old_state_loads_standalone(tmp_path):
    """A state file written before the membership feature loads fine."""
    import json
    p = tmp_path / "old.json"
    p.write_text(json.dumps({
        "node_id": "reg-old", "version_clock": 5,
        "local_versions": {}, "tombstones": {},
    }), encoding="utf-8")
    state = ClusterState.load(path=p)
    assert state.cluster_id is None
    assert state.last_roster == []
    assert state.my_membership_version is None


def test_membership_records_isolated_from_service_read_path(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])

    # Membership lives only in the roster overlay — never leaks into the
    # service foreign overlay / read path / state summary foreign counts.
    for ds in ("ds", "__cluster__", A.membership.cluster_id):
        assert A.foreign_rows(ds) == []
        assert A.foreign_entry(ds, "A:x") is None
    assert A.state_summary()["foreign_records"] == 0


def test_join_requires_admin_token_when_auth_on(tmp_path):
    t = InProcessTransport()
    admin = "admin-tok"
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t,
                    auth_store=_Auth({admin: _Ctx(is_admin=True)}))

    # No token → B rejects the join (unauthorized), B stays standalone.
    res = A.membership.set_add([{"address": "B"}])
    assert not res["results"][0]["ok"]
    assert B.membership.cluster_id is None

    # Admin token → accepted.
    res = A.membership.set_add([{"address": "B"}], token=admin)
    assert res["results"][0]["ok"]
    assert B.membership.cluster_id == A.membership.cluster_id


def test_evict_and_leave_require_auth(tmp_path):
    """On an auth cluster, /evicted and /leave can't be spoofed by an
    unauthenticated caller (forced-partition / forged-removal DoS)."""
    t = InProcessTransport()
    admin = "adm"
    auth = _Auth({admin: _Ctx(is_admin=True)})  # uniform auth across the cluster
    A = build_store(tmp_path, "A", FakeRegistry(), t, auth_store=auth)
    B = build_store(tmp_path, "B", FakeRegistry(), t, auth_store=auth)
    A.membership.set_add([{"address": "B"}], token=admin)
    assert B.membership.cluster_id is not None

    # Unauthenticated / wrong-token control RPCs are rejected; B stays in.
    assert B.membership.handle_evicted({"from_node": "A"})["ok"] is False
    assert B.membership.handle_evicted({"from_node": "A", "token": "WRONG"})["ok"] is False
    assert B.membership.handle_evict_self({"from_node": "A", "token": "WRONG"})["ok"] is False
    assert B.membership.cluster_id is not None

    # The legitimate removal path (proper session token) still works.
    A.membership.set_remove([{"node_id": "B"}])
    assert B.membership.cluster_id is None


def test_membership_tombstone_gc(tmp_path):
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    A.membership.set_remove([{"node_id": "B"}])
    tomb = A.membership._roster["B"]
    assert tomb.removed
    ret = int(A.config.tombstone_retention * 1000)
    # Within retention: kept (so peers/restarts can't resurrect B).
    assert A.membership.gc_membership(now_ms=tomb.version[0] + ret - 1) == 0
    assert "B" in A.membership._roster
    # Past retention: pruned (bounded memory).
    assert A.membership.gc_membership(now_ms=tomb.version[0] + ret + 1) == 1
    assert "B" not in A.membership._roster


def test_restart_keeps_tombstone_no_resurrection(tmp_path):
    """A node removed while a peer was down stays removed after that peer
    restarts: the tombstone survives restart and wins LWW over a stale live
    record."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    C = build_store(tmp_path, "C", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}, {"address": "C"}])
    settle([A, B, C])
    A.membership.set_remove([{"node_id": "C"}])
    settle([A, B, C])

    # "Restart" A from disk → the C tombstone is still there.
    state = ClusterState.load(path=tmp_path / "A.json")
    A2 = ClusterStoreFromState(state, t)
    A2.membership = MembershipStore(A2)
    t.register("A", A2)
    assert A2.membership._roster["C"].removed

    # A stale LIVE record for C (older version) cannot resurrect it.
    A2.membership.merge([{
        "node_id": "C", "cluster_id": A2.membership.cluster_id,
        "address": "C", "version": [1, "C"], "removed": False,
    }])
    assert "C" not in {m["node_id"] for m in A2.membership.show()["roster"]}


def test_set_add_unreachable_member_reports_failure(tmp_path):
    """An unreachable member is reported ok:false (no silent half-join); the
    coordinator doesn't gain a phantom roster entry for it."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    res = A.membership.set_add([{"address": "B"}, {"address": "GHOST"}])
    by_addr = {r["address"]: r for r in res["results"]}
    assert by_addr["B"]["ok"] is True
    assert by_addr["GHOST"]["ok"] is False
    # Only the reachable member is in the roster.
    assert _ids(A.membership.show()) == {"A", "B"}


def test_set_add_to_existing_cluster_keeps_id(tmp_path):
    """Adding a member to an existing cluster reuses the cluster_id."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    C = build_store(tmp_path, "C", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    cid = A.membership.cluster_id
    A.membership.set_add([{"address": "C"}])
    settle([A, B, C])
    assert A.membership.cluster_id == cid
    for s in (A, B, C):
        assert s.membership.cluster_id == cid
        assert _ids(s.membership.show()) == {"A", "B", "C"}


def test_set_remove_then_re_add_rejoins(tmp_path):
    """A removed member can be added back (the tombstone is superseded)."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    A.membership.set_remove([{"node_id": "B"}])
    settle([A, B])
    assert "B" not in _ids(A.membership.show())
    assert B.membership.cluster_id is None

    # Re-add B → it rejoins the same cluster (re-add out-versions the tombstone).
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    assert "B" in _ids(A.membership.show())
    assert not A.membership._roster["B"].removed
    assert B.membership.cluster_id == A.membership.cluster_id


def test_leave_old_preserves_other_cluster_records(tmp_path):
    """leave_old drops only the old cluster's records, keeping ourselves and
    any record from another cluster a concurrent merge may have learned."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    old = A.membership.cluster_id
    # Simulate a record from a different cluster already in the overlay.
    A.membership._roster["Z"] = MembershipRecord("Z", "clu-other", "Z", (99, "Z"), False)

    A.membership.leave_old(old)

    assert "B" not in A.membership._roster          # old-cluster member dropped
    assert "A" in A.membership._roster              # self kept
    assert "Z" in A.membership._roster              # other-cluster record kept


def test_node_learns_removal_via_anti_entropy(tmp_path):
    """Even if the direct /evicted notify is lost, a removed node that pulls a
    peer's tombstone via anti-entropy reverts to standalone."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}])
    settle([A, B])
    cid = B.membership.cluster_id

    # Simulate B receiving its own removal tombstone via merge (anti-entropy)
    # rather than the direct notify.
    tomb_ver = [B.membership._roster["B"].version[0] + 1000, "A"]
    changed = B.membership.merge([{
        "node_id": "B", "cluster_id": cid, "address": "B",
        "version": tomb_ver, "removed": True,
    }])
    assert changed is True
    assert B.membership.cluster_id is None             # reverted to standalone
    assert B.list_peers() == []                        # dropped its sessions


def test_reconcile_keeps_unreachable_member_drops_removed(tmp_path):
    """reconcile_connections must not disconnect a transiently-unreachable but
    still-wanted member; it must disconnect a removed one."""
    t = InProcessTransport()
    A = build_store(tmp_path, "A", FakeRegistry(), t)
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    C = build_store(tmp_path, "C", FakeRegistry(), t)
    A.membership.set_add([{"address": "B"}, {"address": "C"}])
    settle([A, B, C])
    assert {"B", "C"} <= {p.node_id for p in A.list_peers()}

    # C becomes unreachable but stays in the roster → must NOT be dropped.
    t.cut("A", "C")
    A.membership.reconcile_connections()
    assert "C" in {p.node_id for p in A.list_peers()}

    # After an explicit removal it IS dropped.
    t.dropped.clear()
    A.membership.set_remove([{"node_id": "C"}])
    A.membership.reconcile_connections()
    assert "C" not in {p.node_id for p in A.list_peers()}


# ── helper: rebuild a ClusterStore from an existing state (restart sim) ──

def ClusterStoreFromState(state, transport):
    from a2x_registry.cluster.store import ClusterStore
    from a2x_registry.cluster.config import ClusterConfig
    return ClusterStore(
        state, config=ClusterConfig(), registry_svc=FakeRegistry(),
        transport=transport, advertise=state.node_id,
        auth_store_getter=(lambda: None),
    )
