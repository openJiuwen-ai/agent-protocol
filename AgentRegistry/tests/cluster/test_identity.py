"""Unit tests for cluster identity / persisted state.

Uses explicit ``path=`` so these tests never touch the real home dir and
don't depend on the ``A2X_REGISTRY_CLUSTER_STATE`` env var.
"""

from __future__ import annotations

import json

import pytest

from a2x_registry.cluster.state import (
    ClusterState,
    Tombstone,
    generate_node_id,
    make_key,
    split_key,
)
from a2x_registry.cluster.store import ClusterStore


def test_generate_node_id_unique_and_prefixed():
    ids = {generate_node_id() for _ in range(100)}
    assert len(ids) == 100
    assert all(i.startswith("reg-") for i in ids)


def test_key_roundtrip():
    k = make_key("ds1", "generic_abc")
    assert split_key(k) == ("ds1", "generic_abc")


def test_init_writes_file(tmp_path):
    p = tmp_path / "cluster_state.json"
    state = ClusterState.init(node_id="reg-fixed", path=p)
    assert state.node_id == "reg-fixed"
    assert p.exists()
    raw = json.loads(p.read_text(encoding="utf-8"))
    assert raw == {
        "node_id": "reg-fixed",
        "version_clock": 0,
        "local_versions": {},
        "tombstones": {},
        "cluster_id": None,
        "last_roster": [],
        "my_membership_version": None,
    }


def test_init_twice_raises(tmp_path):
    p = tmp_path / "cluster_state.json"
    ClusterState.init(node_id="reg-1", path=p)
    with pytest.raises(FileExistsError):
        ClusterState.init(node_id="reg-2", path=p)


def test_load_absent_returns_none(tmp_path):
    assert ClusterState.load(path=tmp_path / "missing.json") is None


def test_save_load_roundtrip(tmp_path):
    p = tmp_path / "cluster_state.json"
    state = ClusterState.init(node_id="reg-rt", path=p)
    state.version_clock = 42
    state.local_versions[make_key("ds", "sid1")] = [1000, "reg-rt"]
    state.tombstones[make_key("ds", "sid2")] = Tombstone(
        version=(2000, "reg-rt"), deleted_at_ms=2000,
    )
    state.save()

    loaded = ClusterState.load(path=p)
    assert loaded is not None
    assert loaded.node_id == "reg-rt"
    assert loaded.version_clock == 42
    assert loaded.local_versions[make_key("ds", "sid1")] == [1000, "reg-rt"]
    tomb = loaded.tombstones[make_key("ds", "sid2")]
    assert tomb.version == (2000, "reg-rt")
    assert tomb.deleted_at_ms == 2000


def test_membership_fields_roundtrip(tmp_path):
    """cluster_id / last_roster / my_membership_version survive save+load."""
    p = tmp_path / "cluster_state.json"
    state = ClusterState.init(node_id="reg-m", path=p)
    state.cluster_id = "clu-abc123"
    state.last_roster = [
        {"node_id": "B", "cluster_id": "clu-abc123", "address": "http://b:8000",
         "version": [10, "B"], "removed": False},
        {"node_id": "C", "cluster_id": "clu-abc123", "address": "http://c:8000",
         "version": [20, "A"], "removed": True},  # a tombstone must persist
    ]
    state.my_membership_version = [5, "reg-m"]
    state.save()

    loaded = ClusterState.load(path=p)
    assert loaded.cluster_id == "clu-abc123"
    assert loaded.my_membership_version == [5, "reg-m"]
    by_id = {r["node_id"]: r for r in loaded.last_roster}
    assert by_id["B"]["removed"] is False and by_id["B"]["version"] == [10, "B"]
    assert by_id["C"]["removed"] is True  # removal tombstone preserved


def test_load_or_none_corrupt_file_is_defensive(tmp_path, monkeypatch):
    """A corrupt cluster_state.json must not crash startup — load_or_none
    logs and returns None (registry stays standalone)."""
    p = tmp_path / "cluster_state.json"
    p.write_text("{ this is not valid json", encoding="utf-8")
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_STATE", str(p))
    assert ClusterStore.load_or_none() is None


def test_load_or_none_absent_returns_none(tmp_path, monkeypatch):
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_STATE", str(tmp_path / "nope.json"))
    assert ClusterStore.load_or_none() is None


def test_load_or_none_loads_initialized(tmp_path, monkeypatch):
    p = tmp_path / "cluster_state.json"
    ClusterState.init(node_id="reg-loaded", path=p)
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_STATE", str(p))
    store = ClusterStore.load_or_none()
    assert store is not None
    assert store.node_id == "reg-loaded"
    summary = store.state_summary()
    assert summary["node_id"] == "reg-loaded"
    assert summary["peers"] == []
