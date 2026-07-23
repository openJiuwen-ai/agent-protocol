"""Deterministic ID + timestamp helpers for the registry.

- now_iso(): UTC ISO8601 timestamp (second precision, 'Z' suffix).
- image_sid(framework, framework_version): deterministic service_id for an
  image row — "image_" + sha256(framework|framework_version)[:16].
- instance_sid(user, framework): deterministic service_id for an instance
  row — "generic_" + sha256(user|framework)[:8].

These helpers are pure functions (no I/O, no globals) so they can be reused
by the image / instance modules and by tests without a backend.
"""

from __future__ import annotations

import hashlib
from datetime import datetime, timezone


def now_iso() -> str:
    """Return current UTC time as an ISO8601 string with a 'Z' suffix.

    Second precision is sufficient for registry timestamps (created_at /
    updated_at). The fixed format guarantees lexicographic sortability.
    """
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def image_sid(framework: str, framework_version: str) -> str:
    """Return the deterministic service_id for an image row.

    Formula: "image_" + sha256(framework + "|" + framework_version)[:16].
    The same (framework, framework_version) always yields the same id, so a
    repeated register upserts the existing row instead of creating a
    duplicate.
    """
    raw = f"{framework}|{framework_version}"
    digest = hashlib.sha256(raw.encode("utf-8")).hexdigest()
    return "image_" + digest[:16]


def instance_sid(user: str, framework: str) -> str:
    """Return the deterministic service_id for an instance row.

    Formula: "generic_" + sha256(user + "|" + framework)[:8]. The same
    (user, framework) always yields the same id, guaranteeing one row per
    (user, framework) pair (single-instance-per-user-per-framework).
    """
    raw = f"{user}|{framework}"
    digest = hashlib.sha256(raw.encode("utf-8")).hexdigest()
    return "generic_" + digest[:8]
