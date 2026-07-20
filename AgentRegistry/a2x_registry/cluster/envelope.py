"""Sync envelope + LWW version helpers.

A ``SyncEnvelope`` wraps one registry record (or a deletion tombstone) with
the metadata the replication layer needs:

  - ``origin_id`` — the node that owns the record; with origin-only writes
    the global identity is ``(dataset, origin_id, service_id)``.
  - ``version``   — the LWW key ``(updated_at_ms, node_id)``; strictly
    increasing per record, compared lexicographically (timestamp first,
    node id as a deterministic tiebreak).
  - ``tombstone`` — a deletion is just a write whose version can win LWW
    over a stale live value; ``payload`` is ``None`` for tombstones.
"""

from __future__ import annotations

from typing import Optional, Tuple

from pydantic import BaseModel, field_validator

# (updated_at_ms, node_id)
Version = Tuple[int, str]


class SyncEnvelope(BaseModel):
    dataset: str
    service_id: str
    origin_id: str
    version: Version
    tombstone: bool = False
    payload: Optional[dict] = None

    @field_validator("version", mode="before")
    @classmethod
    def _coerce_version(cls, v):
        # JSON round-trips the tuple as a list; coerce back so comparisons
        # stay tuple-vs-tuple.
        if isinstance(v, list):
            return tuple(v)
        return v

    @property
    def key(self) -> Tuple[str, str, str]:
        """Global identity key."""
        return (self.dataset, self.origin_id, self.service_id)


def version_newer(a: Version, b: Optional[Version]) -> bool:
    """True if version ``a`` should win over ``b`` under LWW.

    ``b is None`` (we've never seen the record) → ``a`` always wins.
    Otherwise strict lexicographic ``>`` on ``(updated_at_ms, node_id)``.
    Equal versions are NOT newer → idempotent, and the relay/dedup loop
    terminates (an echo of the same version is dropped).
    """
    if b is None:
        return True
    return tuple(a) > tuple(b)
