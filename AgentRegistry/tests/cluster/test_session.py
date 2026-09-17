"""Session handshake: OPEN establishes symmetric, idempotent sessions."""

from __future__ import annotations


def test_handshake_creates_symmetric_sessions(cluster_pair):
    A, regA, B, regB = cluster_pair
    regA.add_generic("ds1", "a-svc")
    regB.add_generic("ds2", "b-svc")

    peer = A.connect_peer("B")
    assert peer.node_id == "B"

    # A has a session for B, B has a session for A.
    a_peers = {p["node_id"] for p in A.state_summary()["peers"]}
    b_peers = {p["node_id"] for p in B.state_summary()["peers"]}
    assert a_peers == {"B"}
    assert b_peers == {"A"}

    # Accepted namespaces are the union of both sides' datasets.
    assert peer.namespaces == {"ds1", "ds2"}


def test_handshake_idempotent(cluster_pair):
    A, regA, B, regB = cluster_pair
    regA.add_generic("ds1", "a-svc")
    A.connect_peer("B")
    A.connect_peer("B")  # again
    assert len(A.state_summary()["peers"]) == 1
    assert len(B.state_summary()["peers"]) == 1


def test_both_sides_connect_no_duplicate(cluster_pair):
    A, regA, B, regB = cluster_pair
    regA.add_generic("ds1", "a-svc")
    regB.add_generic("ds2", "b-svc")
    A.connect_peer("B")
    B.connect_peer("A")
    # Sessions are keyed by node id → exactly one each direction.
    assert {p["node_id"] for p in A.state_summary()["peers"]} == {"B"}
    assert {p["node_id"] for p in B.state_summary()["peers"]} == {"A"}


def test_disconnect_drops_session_and_foreign(cluster_pair):
    A, regA, B, regB = cluster_pair
    regB.add_generic("ds2", "b-svc")
    A.connect_peer("B")
    assert A.state_summary()["foreign_records"] == 1

    assert A.disconnect_peer("B") is True
    assert A.state_summary()["peers"] == []
    assert A.state_summary()["foreign_records"] == 0
    assert A.disconnect_peer("B") is False  # idempotent
