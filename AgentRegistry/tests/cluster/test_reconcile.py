"""Initial full reconcile: both sides converge after one connect."""

from __future__ import annotations


def test_reconcile_bidirectional_convergence(cluster_pair):
    A, regA, B, regB = cluster_pair
    sid_a = regA.add_generic("ds1", "a-svc")
    sid_b = regB.add_generic("ds2", "b-svc")

    A.connect_peer("B")

    # A learned B's record (origin=B) and B learned A's (origin=A).
    a_foreign = A.foreign_wrapped("ds2")
    assert len(a_foreign) == 1
    assert a_foreign[0]["id"] == f"B:{sid_b}"
    assert a_foreign[0]["origin_id"] == "B"
    assert a_foreign[0]["name"] == "b-svc"

    b_foreign = B.foreign_wrapped("ds1")
    assert len(b_foreign) == 1
    assert b_foreign[0]["id"] == f"A:{sid_a}"
    assert b_foreign[0]["origin_id"] == "A"


def test_same_name_service_no_collision(cluster_pair):
    """Two instances registering a same-named service get the same sid;
    they must coexist (distinct origins), not overwrite each other."""
    A, regA, B, regB = cluster_pair
    sid_a = regA.add_generic("shared", "translator")
    sid_b = regB.add_generic("shared", "translator")
    assert sid_a == sid_b  # same name → same hash → same service_id

    A.connect_peer("B")

    # A keeps its local record AND B's foreign one under a namespaced id.
    rows = A.foreign_wrapped("shared")
    assert len(rows) == 1
    assert rows[0]["id"] == f"B:{sid_b}"
    # The local one is unaffected (still resolvable via the registry).
    assert regA.get_entry("shared", sid_a) is not None


def test_reconcile_newer_version_wins(cluster_pair):
    A, regA, B, regB = cluster_pair
    regB.add_generic("ds", "svc", description="old")
    A.connect_peer("B")
    assert A.foreign_wrapped("ds")[0]["description"] == "old"

    # B updates the record (new content + bumped version), re-sync.
    sid = regB.add_generic("ds", "svc", description="new")  # same sid, replaces
    # bump B's version for the record so it wins LWW
    from a2x_registry.cluster.state import make_key
    B._state.local_versions[make_key("ds", sid)] = [B._next_ts(), "B"]
    A.reconcile(A._sessions["B"])
    assert A.foreign_wrapped("ds")[0]["description"] == "new"


def test_idempotent_updates_no_change_on_reconcile(cluster_pair):
    A, regA, B, regB = cluster_pair
    regB.add_generic("ds", "svc")
    A.connect_peer("B")
    before = A.state_summary()["foreign_records"]
    # Reconciling again with no changes accepts nothing new.
    res = A.reconcile(A._sessions["B"])
    assert res["pulled"] == 0
    assert A.state_summary()["foreign_records"] == before
