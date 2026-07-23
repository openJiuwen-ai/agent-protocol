"""Client-side credential resolution + cli_token.json file IO.

The SDK reads credentials from two places only (matching the design in
``docs/auth_design.md``):

  1. Explicit constructor args: ``A2XRegistryClient(api_key="a2x_pat_...")``
  2. ``~/.a2x_registry_client/cli_token.json`` — written by
     ``a2x-registry-client login`` (or hand-edited as a last resort)

There is NO environment-variable code path. env vars leak across process boundaries (subshells, cron,
IDE consoles) too easily; explicit ``api_key=`` argument or persisted
config file are the only two supported credential surfaces.
"""

from __future__ import annotations

import json
import logging
import os
import stat
import sys
import warnings
from pathlib import Path
from typing import Optional, Tuple

logger = logging.getLogger(__name__)

DEFAULT_BASE_URL = "http://127.0.0.1:8000"
# Config file lives alongside ``owned.json`` to keep the SDK's on-disk
# footprint to a single dotted directory.
DEFAULT_CONFIG_PATH: Path = Path.home() / ".a2x_registry_client" / "cli_token.json"


def read_cli_token(path: Optional[Path] = None) -> Optional[dict]:
    """Read the cli_token.json config file.

    Returns the parsed dict (with at least ``api_key`` key), or ``None``
    if the file doesn't exist. Malformed JSON returns ``None`` + warning
    instead of raising — degraded credential resolution is preferred over
    a hard crash at SDK construction time.

    On POSIX, warns once if the file is group/other-readable: the SDK
    can't refuse to read it (the user may have legitimately relaxed
    perms), but it should be loud so the operator notices.
    """
    path = path or DEFAULT_CONFIG_PATH
    if not path.exists():
        return None
    try:
        # Permission sanity check (POSIX only — Windows uses ACLs).
        if os.name == "posix":
            mode = path.stat().st_mode
            if mode & (stat.S_IRGRP | stat.S_IROTH):
                warnings.warn(
                    f"{path} is readable by group/other; consider 'chmod 600 {path}'",
                    stacklevel=2,
                )
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            warnings.warn(f"{path} is not a JSON object; ignoring", stacklevel=2)
            return None
        return data
    except (OSError, json.JSONDecodeError) as exc:
        warnings.warn(f"Failed to read {path}: {exc}", stacklevel=2)
        return None


def write_cli_token(
    api_key: str,
    base_url: str = DEFAULT_BASE_URL,
    path: Optional[Path] = None,
) -> Path:
    """Persist ``(base_url, api_key)`` to cli_token.json with 0600 perms.

    Atomic: writes to ``<path>.tmp`` then ``os.replace`` so a crash in
    the middle of write can't leave a half-formed credential file.
    Returns the final path.
    """
    if not isinstance(api_key, str) or not api_key.strip():
        raise ValueError("api_key must be a non-empty string")
    path = path or DEFAULT_CONFIG_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    payload = {"base_url": base_url, "api_key": api_key}
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    # chmod 0600 on POSIX. Windows: relies on the user's profile ACL —
    # %USERPROFILE% is owner-only by default; documenting this as
    # adequate for Phase 1 rather than over-engineering ACL handling.
    if os.name == "posix":
        try:
            os.chmod(path, 0o600)
        except OSError as exc:
            warnings.warn(f"Could not chmod 0600 on {path}: {exc}", stacklevel=2)
    return path


def remove_cli_token(path: Optional[Path] = None) -> bool:
    """Idempotent delete. Returns True if a file was removed, False if absent."""
    path = path or DEFAULT_CONFIG_PATH
    if not path.exists():
        return False
    try:
        path.unlink()
        return True
    except OSError as exc:
        warnings.warn(f"Could not remove {path}: {exc}", stacklevel=2)
        return False


def resolve_credentials(
    api_key: Optional[str] = None,
    base_url: Optional[str] = None,
    config_path: Optional[Path] = None,
) -> Tuple[Optional[str], str]:
    """Resolve ``(api_key, base_url)`` per the SDK's precedence rules.

    Precedence:
        1. Explicit ``api_key`` / ``base_url`` constructor args (highest)
        2. Values from ``cli_token.json`` (if present)
        3. Defaults (``api_key=None``, ``base_url=DEFAULT_BASE_URL``)

    Returns ``(api_key, base_url)``. ``api_key`` may still be ``None`` if
    no token is configured anywhere — the SDK then sends no
    ``Authorization`` header and only public / anonymous-namespace
    endpoints will succeed.

    Reads cli_token.json once; if both args are explicit, the file is
    not touched (so test code passing both args doesn't trip the perms
    warning).
    """
    if api_key is not None and base_url is not None:
        return api_key, base_url
    cfg = read_cli_token(config_path)
    file_api_key = (cfg or {}).get("api_key")
    file_base_url = (cfg or {}).get("base_url")
    resolved_api_key = api_key if api_key is not None else (
        file_api_key if isinstance(file_api_key, str) and file_api_key.strip() else None
    )
    resolved_base_url = base_url if base_url is not None else (
        file_base_url if isinstance(file_base_url, str) and file_base_url.strip()
        else DEFAULT_BASE_URL
    )
    return resolved_api_key, resolved_base_url
