"""Tombstone semantics: no resurrection, bounded GC, restart persistence."""

from __future__ import annotations

from a2x_registry.cluster.envelope import SyncEnvelope
from a2x_registry.cluster.state import ClusterState, make_key

from .helpers import FakeRegistry, InProcessTransport, build_store


def _env(origin, sid, ver, *, tombstone=False, desc="d"):
    return SyncEnvelope(
        dataset="ds", service_id=sid, origin_id=origin, version=ver,
        tombstone=tombstone,
        payload=None if tombstone else {
            "entry": {}, "wrapped": {"id": sid, "name": "x", "description": desc,
                                     "type": "generic", "metadata": {}},
        },
    )


def test_local_delete_not_resurrected_by_self_origin_push(tmp_path):
    """A peer echoing our own (deleted) record back is always ignored."""
    t = InProcessTransport()
    rA = FakeRegistry()
    A = build_store(tmp_path, "A", rA, t)
    sid = rA.add_generic("ds", "x")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))
    rA.remove("ds", sid)
    A.on_local_mutation("ds", sid, "deregister", None)

    # A stale push of A's own old record — ignored (self-origin authority).
    assert A.apply_inbound(_env("A", sid, (1, "A"))) is False


def test_foreign_tombstone_blocks_stale_resurrection(tmp_path):
    t = InProcessTransport()
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    sid = "generic_x"
    assert B.apply_inbound(_env("A", sid, (100, "A"), desc="v1")) is True
    assert B.apply_inbound(_env("A", sid, (200, "A"), tombstone=True)) is True
    assert B.foreign_wrapped("ds") == []  # deleted from the read view
    # A stale live version older than the tombstone is rejected.
    assert B.apply_inbound(_env("A", sid, (150, "A"), desc="stale")) is False
    assert B.foreign_wrapped("ds") == []


def test_local_tombstone_gc_after_retention(tmp_path):
    t = InProcessTransport()
    rA = FakeRegistry()
    A = build_store(tmp_path, "A", rA, t)
    sid = rA.add_generic("ds", "x")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))
    rA.remove("ds", sid)
    A.on_local_mutation("ds", sid, "deregister", None)
    assert len(A._state.tombstones) == 1

    deleted_at = A._state.tombstones[make_key("ds", sid)].deleted_at_ms
    retention_ms = int(A.config.tombstone_retention * 1000)
    # Within retention: kept.
    assert A.gc_tombstones(now_ms=deleted_at + retention_ms - 1) == 0
    assert len(A._state.tombstones) == 1
    # Past retention: collected.
    assert A.gc_tombstones(now_ms=deleted_at + retention_ms + 1) == 1
    assert len(A._state.tombstones) == 0


def test_foreign_tombstone_gc_after_retention(tmp_path):
    t = InProcessTransport()
    B = build_store(tmp_path, "B", FakeRegistry(), t)
    B.apply_inbound(_env("A", "generic_x", (1000, "A"), tombstone=True))
    retention_ms = int(B.config.tombstone_retention * 1000)
    assert B.gc_tombstones(now_ms=1000 + retention_ms - 1) == 0
    assert B.gc_tombstones(now_ms=1000 + retention_ms + 1) == 1


def test_local_tombstone_persists_across_restart(tmp_path):
    t = InProcessTransport()
    rA = FakeRegistry()
    A = build_store(tmp_path, "A", rA, t)
    sid = rA.add_generic("ds", "x")
    A.on_local_mutation("ds", sid, "register", rA.get_entry("ds", sid))
    rA.remove("ds", sid)
    A.on_local_mutation("ds", sid, "deregister", None)

    # Reload state from disk → tombstone (and its version) survived.
    reloaded = ClusterState.load(path=tmp_path / "A.json")
    assert make_key("ds", sid) in reloaded.tombstones
    assert make_key("ds", sid) not in reloaded.local_versions
