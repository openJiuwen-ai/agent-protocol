"""Cluster test harness: in-process transport + fake registry.

Two ``ClusterStore`` instances can't share one process via the FastAPI
module-singleton, so component tests wire them directly through an
``InProcessTransport`` that routes a peer ``address`` straight to the
target store's handler methods (the same methods the HTTP router calls).
This exercises the full sync logic without real servers.
"""

from __future__ import annotations

import pytest

from a2x_registry.cluster.state import ClusterState

from .helpers import FakeRegistry, InProcessTransport, build_store


@pytest.fixture
def cluster_app(tmp_path, monkeypatch):
    """Boot a real (lite) backend with the cluster module initialized.

    Exercises the HTTP router + store handlers on the *receiver* side; a
    simulated peer drives ``/api/cluster/*`` over the TestClient.
    """
    import importlib.util
    import sys

    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    real_find_spec = importlib.util.find_spec
    monkeypatch.setattr(
        importlib.util, "find_spec",
        lambda n, *a, **k: None if n in heavy else real_find_spec(n, *a, **k),
    )
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))

    state_file = tmp_path / "cluster_state.json"
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_STATE", str(state_file))
    ClusterState.init(node_id="B", path=state_file)

    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(sys.modules, n, raising=False)

    from a2x_registry.backend.app import app
    from a2x_registry.backend.startup import run_warmup
    run_warmup()

    from fastapi.testclient import TestClient
    return TestClient(app)


@pytest.fixture
def cluster_auth_app(cluster_app, tmp_path):
    """cluster_app + a bootstrapped AuthStore, so the cluster module sees
    auth as ON (it reads get_auth_store dynamically). Yields (client,
    admin_token)."""
    from a2x_registry.auth.store import AuthStore
    from a2x_registry.auth.deps import set_auth_store

    store, token = AuthStore.bootstrap(data_dir=tmp_path / "auth_data")
    set_auth_store(store)
    try:
        yield cluster_app, token
    finally:
        set_auth_store(None)


@pytest.fixture
def cluster_pair(tmp_path):
    """Two wired instances A and B with fresh fake registries.

    Returns ``(A, regA, B, regB)``. Addresses equal the node ids
    ("A" / "B"); ``connect_peer("B")`` dials store B in-process.
    """
    transport = InProcessTransport()
    regA, regB = FakeRegistry(), FakeRegistry()
    A = build_store(tmp_path, "A", regA, transport)
    B = build_store(tmp_path, "B", regB, transport)
    return A, regA, B, regB
