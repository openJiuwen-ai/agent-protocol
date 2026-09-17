"""Bucketed Merkle digest for service-plane anti-entropy.

Instead of shipping the whole version index every reconcile, two nodes first
exchange one hash per **bucket** (records are partitioned into a fixed number
of buckets by a hash of their key). Buckets whose hashes match are provably
identical, so only the differing buckets transfer their rows. Steady state
(no changes) costs O(buckets), not O(records).

Determinism (customer requirement): bucketing and hashing are pure functions
of (key, version) — no randomness, so two nodes with the same data always
produce identical bucket hashes.
"""

from __future__ import annotations

import hashlib
from typing import Dict, Iterable, Tuple

# index key = (dataset, origin_id, service_id); version = (updated_at_ms, node_id)
_Key = Tuple[str, str, str]
_Version = Tuple[int, str]

_SEP = "\x1f"  # unit separator — can't appear in ids


def _key_str(key: _Key) -> str:
    return _SEP.join(key)


def bucket_of(key: _Key, n_buckets: int) -> int:
    """Deterministic bucket index for a key (stable across nodes)."""
    h = hashlib.sha256(_key_str(key).encode("utf-8")).digest()
    return int.from_bytes(h[:4], "big") % n_buckets


def _entry_digest(key: _Key, version: _Version) -> bytes:
    raw = f"{_key_str(key)}{_SEP}{version[0]}{_SEP}{version[1]}"
    return hashlib.sha256(raw.encode("utf-8")).digest()


def bucket_hashes(index: Dict[_Key, _Version], n_buckets: int) -> Dict[str, str]:
    """Map ``{bucket_index_str: hex_hash}`` for every non-empty bucket. A
    bucket's hash folds its entries in sorted order so it's order-independent.
    Keys are stringified (``str(int)``) so the map survives JSON round-trips."""
    buckets: Dict[int, list] = {}
    for key, version in index.items():
        buckets.setdefault(bucket_of(key, n_buckets), []).append((key, tuple(version)))
    out: Dict[str, str] = {}
    for b, entries in buckets.items():
        entries.sort()
        h = hashlib.sha256()
        for key, version in entries:
            h.update(_entry_digest(key, version))
        out[str(b)] = h.hexdigest()
    return out


def differing_buckets(local: Dict[str, str], remote: Dict[str, str]) -> set:
    """Bucket indices (ints) present on either side with differing hashes."""
    diff = set()
    for b in set(local) | set(remote):
        if local.get(b) != remote.get(b):
            diff.add(int(b))
    return diff


def keys_in_buckets(keys: Iterable[_Key], buckets: set, n_buckets: int) -> set:
    return {k for k in keys if bucket_of(k, n_buckets) in buckets}
