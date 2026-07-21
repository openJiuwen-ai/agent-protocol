"""Persisted cluster state — node identity + local version map + tombstones.

This is the ONLY thing the cluster module writes to disk. Foreign
(replicated) records are deliberately memory-only (re-synced on
reconnect), but three things must survive a restart:

  - ``node_id``       — stable global identity for this instance.
  - ``version_clock`` — last emitted version timestamp (ms); seeds the
    monotonic guard so a wall-clock step-back can't make a new local write
    look older than a previous one.
  - ``local_versions``/``tombstones`` — the version of every local-origin
    record and of every local deletion, so that after a restart we can
    answer digests authoritatively and a not-yet-propagated delete still
    wins LWW against a peer that pushes the stale record back.

Presence of the state file is what makes the cluster module *opt-in*:
``ClusterStore.load_or_none`` returns ``None`` when it's absent, and the
whole feature stays dormant (endpoints 404, read path unchanged).
"""

from __future__ import annotations

import json
import os
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from a2x_registry.common.atomic import atomic_write_json
from a2x_registry.common.paths import get_home

# Env override (mainly for tests / multi-instance-on-one-host) — points at
# the cluster_state.json file directly. Mirrors auth's A2X_REGISTRY_AUTH_DATA.
ENV_STATE_PATH = "A2X_REGISTRY_CLUSTER_STATE"

# Composite map key: f"{dataset}\x00{service_id}". A NUL separator can't
# appear in either component, so the split is unambiguous.
_SEP = "\x00"


def state_path() -> Path:
    """Resolve the cluster_state.json location.

    1. ``A2X_REGISTRY_CLUSTER_STATE`` env var (explicit file path)
    2. ``<get_home()>/cluster_state.json``
    """
    env = os.environ.get(ENV_STATE_PATH, "").strip()
    if env:
        return Path(env).expanduser()
    return get_home() / "cluster_state.json"


def make_key(dataset: str, service_id: str) -> str:
    return f"{dataset}{_SEP}{service_id}"


def split_key(key: str) -> tuple[str, str]:
    dataset, _, sid = key.partition(_SEP)
    return dataset, sid


def generate_node_id() -> str:
    """A stable, readable, globally-unique node id."""
    return f"reg-{uuid.uuid4().hex[:12]}"


@dataclass
class Tombstone:
    version: tuple  # (updated_at_ms, node_id) — the deletion's LWW version
    deleted_at_ms: int


@dataclass
class ClusterState:
    """In-memory mirror of cluster_state.json. Mutations call ``save``."""

    node_id: str
    version_clock: int = 0
    # composite key -> version tuple (updated_at_ms, node_id)
    local_versions: Dict[str, list] = field(default_factory=dict)
    # composite key -> Tombstone
    tombstones: Dict[str, Tombstone] = field(default_factory=dict)
    # ── membership control plane (added with the cluster-set feature) ──
    # The cluster this node belongs to (None = standalone). Survives restart
    # so the node rejoins its cluster automatically.
    cluster_id: Optional[str] = None
    # Last-known roster as membership-record dicts (live members + recent
    # removal tombstones) — seeds auto-reconnect on restart and preserves the
    # removal window so a node removed while offline isn't resurrected. The
    # live set re-converges via membership anti-entropy once peers reconnect.
    last_roster: List[dict] = field(default_factory=list)
    # Version (updated_at_ms, node_id) of THIS node's own membership record,
    # so a re-adopt after restart stays monotonic under LWW.
    my_membership_version: Optional[list] = None
    path: Optional[Path] = None

    # ── persistence ─────────────────────────────────────────────────────

    @classmethod
    def load(cls, path: Optional[Path] = None) -> Optional["ClusterState"]:
        """Load from disk, or ``None`` if the file doesn't exist."""
        p = Path(path) if path is not None else state_path()
        if not p.exists():
            return None
        with open(p, "r", encoding="utf-8") as f:
            raw = json.load(f)
        tombstones = {
            k: Tombstone(version=tuple(v["version"]), deleted_at_ms=int(v["deleted_at_ms"]))
            for k, v in (raw.get("tombstones") or {}).items()
        }
        mmv = raw.get("my_membership_version")
        return cls(
            node_id=raw["node_id"],
            version_clock=int(raw.get("version_clock", 0)),
            local_versions={k: list(v) for k, v in (raw.get("local_versions") or {}).items()},
            tombstones=tombstones,
            # Forward-compatible: a state file written before the membership
            # feature lacks these keys → defaults keep the node standalone.
            cluster_id=raw.get("cluster_id"),
            last_roster=list(raw.get("last_roster") or []),
            my_membership_version=list(mmv) if mmv else None,
            path=p,
        )

    @classmethod
    def init(cls, node_id: Optional[str] = None, path: Optional[Path] = None) -> "ClusterState":
        """Create + persist a fresh state file. Raises if one already exists."""
        p = Path(path) if path is not None else state_path()
        if p.exists():
            raise FileExistsError(
                f"Cluster already initialized at {p}. Delete it to re-init."
            )
        p.parent.mkdir(parents=True, exist_ok=True)
        state = cls(node_id=node_id or generate_node_id(), path=p)
        state.save()
        return state

    def save(self) -> None:
        """Atomically persist current state to ``self.path``."""
        if self.path is None:
            self.path = state_path()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_json(self.path, self.to_dict())

    def to_dict(self) -> dict:
        return {
            "node_id": self.node_id,
            "version_clock": self.version_clock,
            "local_versions": self.local_versions,
            "tombstones": {
                k: {"version": list(t.version), "deleted_at_ms": t.deleted_at_ms}
                for k, t in self.tombstones.items()
            },
            "cluster_id": self.cluster_id,
            "last_roster": self.last_roster,
            "my_membership_version": (
                list(self.my_membership_version) if self.my_membership_version else None
            ),
        }
