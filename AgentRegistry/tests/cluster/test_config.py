"""ClusterConfig.from_env — deploy-time override of liveness knobs."""

from __future__ import annotations

from a2x_registry.cluster.config import ClusterConfig


def test_defaults_when_no_env(monkeypatch):
    for k in list(__import__("os").environ):
        if k.startswith("A2X_REGISTRY_CLUSTER_"):
            monkeypatch.delenv(k, raising=False)
    cfg = ClusterConfig.from_env()
    assert cfg.hold_timeout == 30.0 and cfg.keepalive_interval == 10.0
    # tombstone_retention = hold_timeout + keepalive_interval
    assert cfg.tombstone_retention == 40.0


def test_override_hold_and_keepalive(monkeypatch):
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_HOLD_TIMEOUT", "60")
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_KEEPALIVE_INTERVAL", "20")
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_ANTI_ENTROPY_INTERVAL", "7.5")
    cfg = ClusterConfig.from_env()
    assert cfg.hold_timeout == 60.0
    assert cfg.keepalive_interval == 20.0
    assert cfg.tombstone_retention == 80.0       # derived, follows the overrides
    assert cfg.anti_entropy_interval == 7.5
    # Untouched knob keeps its default.
    assert cfg.http_timeout == 5.0


def test_override_scale_knobs(monkeypatch):
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_BROADCAST_WORKERS", "64")
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_MERKLE_BUCKETS", "512")
    cfg = ClusterConfig.from_env()
    assert cfg.broadcast_workers == 64 and isinstance(cfg.broadcast_workers, int)
    assert cfg.merkle_buckets == 512 and isinstance(cfg.merkle_buckets, int)


def test_float_field_accepts_int_string(monkeypatch):
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_HOLD_TIMEOUT", "20")
    assert ClusterConfig.from_env().hold_timeout == 20.0


def test_invalid_value_falls_back_to_default(monkeypatch):
    monkeypatch.setenv("A2X_REGISTRY_CLUSTER_HOLD_TIMEOUT", "abc")
    cfg = ClusterConfig.from_env()
    assert cfg.hold_timeout == 30.0              # default, not a crash
