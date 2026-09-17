"""Incremental push, full-mesh direct broadcast, version dedup, no echo."""

from __future__ import annotations

from a2x_registry.cluster.envelope import SyncEnvelope

from .helpers import FakeRegistry, InProcessTransport, build_store


def test_incremental_push_to_existing_namespace(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("ds", "seed")  # ds exists at handshake → in session
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")

    # A registers a new service after the session is up → pushed to B.
    sid = rA.add_generic("ds", "later")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))

    names = {r["name"] for r in B.foreign_wrapped("ds")}
    assert "later" in names


def test_full_mesh_direct_broadcast_reaches_all_peers(tmp_path):
    """Full mesh: A's record reaches every direct peer (B and C) by a single
    broadcast — no relay. A never stores its own record as foreign."""
    t = InProcessTransport()
    rA, rB, rC = FakeRegistry(), FakeRegistry(), FakeRegistry()
    for r in (rA, rB, rC):
        r.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    C = build_store(tmp_path, "C", rC, t)
    A.connect_peer("B")
    A.connect_peer("C")

    sid = rA.add_generic("ds", "from-A")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))

    for node in (B, C):
        assert any(r["origin_id"] == "A" and r["name"] == "from-A"
                   for r in node.foreign_wrapped("ds"))
    # Self-origin is never stored as foreign on A.
    assert all(r["origin_id"] != "A" for r in A.foreign_wrapped("ds"))


def test_inbound_updates_are_not_relayed(tmp_path):
    """No relay: a record A receives from B is stored but NOT forwarded to
    A's other peer C. Full-mesh reachability comes from the origin's own
    broadcast, not from intermediaries re-flooding. Here B and C are NOT
    directly connected, so without relay C never learns B's record."""
    t = InProcessTransport()
    rA, rB, rC = FakeRegistry(), FakeRegistry(), FakeRegistry()
    for r in (rA, rB, rC):
        r.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    C = build_store(tmp_path, "C", rC, t)
    # A is connected to both B and C; B and C are NOT connected to each other.
    A.connect_peer("B")
    A.connect_peer("C")

    # B originates a record → broadcasts only to its direct peer A.
    sid = rB.add_generic("ds", "from-B")
    B.on_local_mutation("ds", sid, "register", rB.get_entry("ds", sid))

    assert any(r["name"] == "from-B" for r in A.foreign_wrapped("ds"))
    # A must not relay it onward to C (no relay) → C never sees it.
    assert all(r["name"] != "from-B" for r in C.foreign_wrapped("ds"))


def test_self_origin_inbound_ignored(tmp_path):
    t = InProcessTransport()
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    env = SyncEnvelope(
        dataset="ds", service_id="generic_x", origin_id="B",
        version=(999, "B"), tombstone=False,
        payload={"entry": {}, "wrapped": {"id": "generic_x", "name": "x"}},
    )
    assert B.apply_inbound(env) is False
    assert B.foreign_wrapped("ds") == []


def test_version_dedup(tmp_path):
    t = InProcessTransport()
    B = build_store(tmp_path, "B", FakeRegistry(), t)

    def env(ver, desc):
        return SyncEnvelope(
            dataset="ds", service_id="generic_x", origin_id="A",
            version=ver, tombstone=False,
            payload={"entry": {}, "wrapped": {"id": "generic_x", "name": "x",
                                              "description": desc, "type": "generic",
                                              "metadata": {}}},
        )

    assert B.apply_inbound(env((100, "A"), "v1")) is True
    assert B.apply_inbound(env((100, "A"), "v1")) is False   # same version
    assert B.apply_inbound(env((50, "A"), "older")) is False  # older
    assert B.apply_inbound(env((200, "A"), "v2")) is True     # newer
    assert B.foreign_wrapped("ds")[0]["description"] == "v2"


def test_deregister_tombstone_propagates(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("ds", "seed")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")

    sid = rA.add_generic("ds", "todelete")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))
    assert any(r["name"] == "todelete" for r in B.foreign_wrapped("ds"))

    rA.remove("ds", sid)
    A.on_local_mutation("ds", sid, "deregister", None)
    # B applied the tombstone → the record disappears from the read view.
    assert all(r["name"] != "todelete" for r in B.foreign_wrapped("ds"))
