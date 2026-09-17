"""Cluster runtime tuning knobs.

A single immutable config object passed to ``ClusterStore`` and the
background daemons. Values are conservative defaults suitable for a small
group of intermittently-connected registry instances.

Operators can override any knob at deploy time via ``A2X_REGISTRY_CLUSTER_*``
environment variables (read by ``ClusterConfig.from_env`` at server start) —
no code change needed. A malformed value logs a warning and falls back to
the default.
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass, fields

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class ClusterConfig:
    # Direct-link keepalive / HOLD timer. In a full mesh every member is a
    # direct peer, so liveness is just this: we keepalive each peer every
    # ``keepalive_interval``; a peer silent past ``hold_timeout`` is dropped
    # and its records evicted.
    keepalive_interval: float = 10.0
    hold_timeout: float = 30.0

    # Periodic anti-entropy reconciliation with each peer.
    anti_entropy_interval: float = 20.0

    # Per-request HTTP timeout for peer calls (seconds).
    http_timeout: float = 5.0

    # Max concurrent peer calls per fan-out (broadcast / keepalive). Bounds
    # a full-mesh broadcast to ~one timeout window instead of N sequential
    # ones, so a dead peer can't stall a local CRUD. ~1000 nodes is fine
    # with a few dozen workers (the calls are I/O-bound).
    broadcast_workers: int = 32

    # Number of buckets for Merkle anti-entropy. Each reconcile first
    # compares ``merkle_buckets`` bucket hashes (O(buckets), not O(records));
    # only differing buckets transfer their rows. Must match cluster-wide
    # (a mismatch degrades to a fuller transfer, still correct).
    merkle_buckets: int = 256

    @property
    def tombstone_retention(self) -> float:
        """Local tombstones (and the post-eviction suppression cooldown) are
        kept at least this long (seconds) before GC. Derived from the HOLD
        window (``hold_timeout + keepalive_interval``) so every peer has had
        time to detect the loss and evict its stale replica before we forget
        the deletion — preventing resurrection."""
        return float(self.hold_timeout + self.keepalive_interval)

    # ── env-var overrides ────────────────────────────────────────────────

    @classmethod
    def from_env(cls) -> "ClusterConfig":
        """Build a config from defaults, overriding any knob present as an
        ``A2X_REGISTRY_CLUSTER_<FIELD>`` env var (e.g.
        ``A2X_REGISTRY_CLUSTER_HOLD_TIMEOUT=60``). Unknown/blank vars keep the
        default; a non-numeric value logs a warning and keeps the default.
        """
        defaults = cls()
        overrides = {}
        for f in fields(cls):
            env_name = f"A2X_REGISTRY_CLUSTER_{f.name.upper()}"
            raw = os.environ.get(env_name, "").strip()
            if not raw:
                continue
            # With `from __future__ import annotations`, f.type is the string
            # "int"/"float". int fields tolerate "10" or "10.0".
            try:
                overrides[f.name] = int(float(raw)) if f.type == "int" else float(raw)
            except (ValueError, TypeError):
                logger.warning(
                    "cluster: ignoring invalid %s=%r (using default %s)",
                    env_name, raw, getattr(defaults, f.name),
                )
        return cls(**overrides) if overrides else defaults
