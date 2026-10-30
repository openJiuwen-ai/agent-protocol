"""Semantic version normalization key for image ordering.

``version_key(framework_version)`` produces a fixed-width sortable string
so that ``ORDER BY version_key DESC`` correctly orders versions:

    v0.2.0  -> 00000.00002.00000~
    v0.10.0 -> 00000.00010.00000~
    v0.2.0-beta -> 00000.00002.00000-beta

The trailing ``~`` (ASCII 0x7E) is higher than any letter or ``-``, so
in DESC order formal releases appear before pre-releases of the same
version. Non-semver strings get a zeroed-out key and sort last within
their framework group (secondary sort by ``created_at``).
"""

from __future__ import annotations

import re

_SEMVER = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:-(.+))?$")


def version_key(framework_version: str) -> str:
    """Convert a framework version string to a normalized sort key.

    Examples:
        >>> version_key("v0.2.0")
        '00000.00002.00000~'
        >>> version_key("v0.10.0")
        '00000.00010.00000~'
        >>> version_key("v0.2.0-beta")
        '00000.00002.00000-beta'
        >>> version_key("nightly")
        '00000.00000.00000'

    Non-semver strings (no match) return all zeros and are sorted last
    within their framework via secondary sort on ``created_at``.
    """
    m = _SEMVER.match(framework_version.strip())
    if not m:
        return "00000.00000.00000"
    major, minor, patch, pre = m.groups()
    base = f"{int(major):05d}.{int(minor):05d}.{int(patch):05d}"
    return f"{base}-{pre}" if pre else f"{base}~"