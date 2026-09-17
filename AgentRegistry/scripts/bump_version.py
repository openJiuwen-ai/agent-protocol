#!/usr/bin/env python3
"""Bump the single shared version across both packages in this monorepo.

The server (`a2x_registry`) and the client SDK (`a2x_registry_client`) are
two independently-installable distributions but share ONE version number.
Both pyprojects read their version dynamically from the package
``__version__``; this script rewrites both (and the root ``VERSION`` file)
in one shot so they can never drift.

Usage:
    python scripts/bump_version.py 0.3.1
    python scripts/bump_version.py --check      # verify all three agree
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "VERSION"
INIT_FILES = [
    ROOT / "a2x_registry" / "__init__.py",
    ROOT / "client" / "a2x_registry_client" / "__init__.py",
]
_VER_RE = re.compile(r'^__version__\s*=\s*["\'][^"\']+["\']', re.M)
_SEMVER = re.compile(r"^\d+\.\d+\.\d+([abrc].*)?$")


def _read_init_version(path: Path) -> str:
    m = re.search(r'__version__\s*=\s*["\']([^"\']+)["\']', path.read_text(encoding="utf-8"))
    return m.group(1) if m else "?"


def check() -> int:
    versions = {
        "VERSION": VERSION_FILE.read_text(encoding="utf-8").strip(),
        **{p.parent.name: _read_init_version(p) for p in INIT_FILES},
    }
    print("\n".join(f"  {k:24} {v}" for k, v in versions.items()))
    if len(set(versions.values())) == 1:
        print(f"OK — all agree on {next(iter(versions.values()))}")
        return 0
    print("MISMATCH — run `python scripts/bump_version.py <version>` to sync")
    return 1


def bump(version: str) -> int:
    if not _SEMVER.match(version):
        print(f"error: {version!r} is not a valid version (expected X.Y.Z)")
        return 2
    VERSION_FILE.write_text(version + "\n", encoding="utf-8")
    for path in INIT_FILES:
        text = path.read_text(encoding="utf-8")
        new = _VER_RE.sub(f'__version__ = "{version}"', text)
        path.write_text(new, encoding="utf-8")
    print(f"bumped to {version}:")
    return check()


def main(argv: list[str]) -> int:
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if argv[0] == "--check":
        return check()
    return bump(argv[0])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
