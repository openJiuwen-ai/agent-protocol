"""Scale-path behaviour: concurrent fan-out (a slow/dead peer doesn't
serialize the rest) and Merkle anti-entropy (no transfer when in sync; only
the differing bucket transfers on a change)."""

from __future__ import annotations

import time

from a2x_registry.cluster.config import ClusterConfig
from a2x_registry.cluster.envelope import SyncEnvelope
from a2x_registry.cluster.peer import Peer
from a2x_registry.cluster.state import ClusterState
from a2x_registry.cluster.store import ClusterStore
from a2x_registry.cluster.transport import Transport

from .helpers import (
    FakeRegistry, InProcessTransport, build_store, converge, settle,
)


class _SleepTransport(Transport):
    """Every ``updates`` call sleeps — simulates slow/dead peers so we can
    show the fan-out runs them concurrently, not one-after-another."""

    def __init__(self, delay: float) -> None:
        self.delay = delay
        self.hits = []

    def register(self, address, store):  # build_store compatibility (unused)
        pass

    def updates(self, address, from_node, envelopes, token=None):
        time.sleep(self.delay)
        self.hits.append(address)
        return {"accepted": len(envelopes)}


def test_broadcast_runs_concurrently(tmp_path):
    delay, k = 0.2, 8
    tr = _SleepTransport(delay)
    store = ClusterStore(
        ClusterState.init(node_id="A", path=tmp_path / "A.json"),
        config=ClusterConfig(broadcast_workers=32),
        registry_svc=FakeRegistry(), transport=tr, advertise="A",
        auth_store_getter=(lambda: None),
    )
    for i in range(k):
        store._sessions[f"P{i}"] = Peer(f"P{i}", f"P{i}", {"ds"})

    env = SyncEnvelope(dataset="ds", service_id="x", origin_id="A",
                       version=(1, "A"), tombstone=False, payload={})
    t0 = time.monotonic()
    store._broadcast(env)
    elapsed = time.monotonic() - t0

    assert len(tr.hits) == k                 # all peers reached
    assert elapsed < delay * k / 2           # concurrent, not k sequential delays
    store.close()


class _CountingTransport(InProcessTransport):
    """Wraps the in-process transport to count anti-entropy RPCs."""

    def __init__(self) -> None:
        super().__init__()
        self.n_merkle = 0
        self.n_digest = 0

    def merkle(self, address, from_node, namespaces, token=None):
        self.n_merkle += 1
        return super().merkle(address, from_node, namespaces, token)

    def digest(self, address, from_node, namespaces, token=None, buckets=None):
        self.n_digest += 1
        return super().digest(address, from_node, namespaces, token, buckets=buckets)


def test_merkle_skips_row_transfer_when_in_sync(tmp_path):
    t = _CountingTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("ds", "a-svc")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")
    converge([A, B])  # fully in sync now

    before = (t.n_merkle, t.n_digest)
    A.reconcile(A.list_peers()[0])
    # In sync → only the bucket-hash digest crosses the wire, no /digest rows.
    assert t.n_merkle == before[0] + 1
    assert t.n_digest == before[1]


def test_merkle_transfers_only_on_change(tmp_path):
    t = _CountingTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rB.add_generic("ds", "seed")  # so "ds" is negotiated into the session
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")
    converge([A, B])

    # B registers a new service while the push to A is dropped → exactly one
    # bucket now differs and only anti-entropy can heal it.
    t.cut("B", "A")
    sid = rB.add_generic("ds", "b-new")
    B.on_local_mutation("ds", sid, "register", rB.get_entry("ds", sid))
    t.dropped.clear()

    d0 = t.n_digest
    res = A.reconcile(A.list_peers()[0])
    assert t.n_digest == d0 + 1          # a bucket differed → one row fetch
    assert res["pulled"] == 1            # pulled the new record
    assert any(r["name"] == "b-new" for r in A.foreign_wrapped("ds"))


def test_http_transport_pools_and_close_guard(tmp_path):
    import pytest
    httpx = pytest.importorskip("httpx")
    from a2x_registry.cluster.transport import HttpTransport, TransportError

    tr = HttpTransport(timeout=1.0)
    c1 = tr._get_client()
    c2 = tr._get_client()
    assert c1 is c2                      # one pooled client reused across calls
    assert isinstance(c1, httpx.Client)

    tr.close()
    tr.close()                           # idempotent
    with pytest.raises(TransportError):  # no lazy re-init after close
        tr._get_client()


def test_store_close_idempotent(tmp_path):
    store = ClusterStore(
        ClusterState.init(node_id="A", path=tmp_path / "A.json"),
        config=ClusterConfig(), registry_svc=FakeRegistry(),
        transport=_SleepTransport(0.0), advertise="A",
        auth_store_getter=(lambda: None),
    )
    store.fan_out([lambda: None])        # spin up the pool
    store.close()
    store.close()                        # second close must not raise
