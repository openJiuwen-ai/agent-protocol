"""Start the A2X Registry backend API server (standalone, no frontend).

Usage:
    a2x-registry                                 # API on http://127.0.0.1:8000
    a2x-registry --port 8080                     # Port override (env wins if set)
    python -m a2x_registry.backend               # equivalent module-form invocation

Listen address comes from ``registry.env`` (env vars), NOT a CLI flag:

    A2X_REGISTRY_MODE        "" (generic) | "appliance"
    A2X_REGISTRY_BIND        empty -> 127.0.0.1 ; concrete IP ; 0.0.0.0 forbidden
    A2X_REGISTRY_PORT        empty -> 8000
    A2X_REGISTRY_HA_MEMBERS  must be empty (single-node SQLite only)
    A2X_REGISTRY_DB_KIND     empty -> sqlite | "memory" (debug) | "rqlite"

Auth admin subcommands (no server needed):
    a2x-registry auth init                       # bootstrap first admin key
    a2x-registry auth reset-admin --confirm      # rotate the bootstrap admin

Cluster subcommands (distributed sync):
    a2x-registry cluster init                    # generate node id (opt-in)
    a2x-registry cluster status                  # show sync state
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass
from typing import Tuple


# ── env var names ────────────────────────────────────────────────────────
_ENV_MODE = "A2X_REGISTRY_MODE"
_ENV_BIND = "A2X_REGISTRY_BIND"
_ENV_PORT = "A2X_REGISTRY_PORT"
_ENV_HA_MEMBERS = "A2X_REGISTRY_HA_MEMBERS"
_ENV_DB_KIND = "A2X_REGISTRY_DB_KIND"

_VALID_MODES = ("", "appliance")
_VALID_DB_KINDS = ("sqlite", "memory", "rqlite")
_DEFAULT_BIND = "127.0.0.1"
_DEFAULT_PORT = 8000
_FORBIDDEN_BIND = "0.0.0.0"


@dataclass(frozen=True)
class RuntimeConfig:
    """Resolved runtime config from env + CLI overrides.

    - ``mode``: "" (generic, service table only) or "appliance" (also
      creates image / instance registries at startup).
    - ``bind``: concrete listen IP. Never "0.0.0.0" (binds
      to a specific interface or loopback only).
    - ``port``: listen port.
    - ``ha_members``: tuple of peer addresses; must be empty
      (single-node SQLite only). Non-empty indicates a later rqlite release.
    - ``db_kind``: storage backend kind — ``sqlite`` (production single-node,
      file-persisted), ``memory`` (debug only, in-process, lost on exit),
      or ``rqlite`` (Raft-replicated cluster; endpoint/auth read in
      ``startup.py``). Empty env var defaults to ``sqlite``.
    """

    mode: str
    bind: str
    port: int
    ha_members: Tuple[str, ...]
    db_kind: str


def parse_runtime_config() -> RuntimeConfig:
    """Parse runtime config from environment variables.

    Raises ``ValueError`` on:
      - unknown ``A2X_REGISTRY_MODE`` (only "" / "appliance" valid)
      - ``A2X_REGISTRY_BIND=0.0.0.0`` (wildcard forbidden)
      - non-integer ``A2X_REGISTRY_PORT``
      - non-empty ``A2X_REGISTRY_HA_MEMBERS`` (single-node only)
      - unknown ``A2X_REGISTRY_DB_KIND`` (only sqlite / memory / rqlite)
    """
    mode = os.environ.get(_ENV_MODE, "").strip()
    if mode not in _VALID_MODES:
        raise ValueError(
            f"unknown A2X_REGISTRY_MODE={mode!r}; "
            f"accepts only '' (generic) or 'appliance'"
        )

    bind = os.environ.get(_ENV_BIND, "").strip() or _DEFAULT_BIND
    if bind == _FORBIDDEN_BIND:
        raise ValueError(
            "A2X_REGISTRY_BIND=0.0.0.0 is forbidden; "
            "bind to a concrete interface or 127.0.0.1"
        )

    port_raw = os.environ.get(_ENV_PORT, "").strip()
    if port_raw:
        try:
            port = int(port_raw)
        except ValueError as exc:
            raise ValueError(
                f"A2X_REGISTRY_PORT={port_raw!r} is not an integer"
            ) from exc
        if port <= 0 or port > 65535:
            raise ValueError(f"A2X_REGISTRY_PORT={port} out of range (1-65535)")
    else:
        port = _DEFAULT_PORT

    ha_raw = os.environ.get(_ENV_HA_MEMBERS, "")
    ha_members = tuple(m.strip() for m in ha_raw.split(",") if m.strip())
    if ha_members:
        raise ValueError(
            "A2X_REGISTRY_HA_MEMBERS is non-empty but current build is "
            "single-node SQLite; rqlite HA is a later release"
        )

    db_kind = os.environ.get(_ENV_DB_KIND, "").strip() or "sqlite"
    if db_kind not in _VALID_DB_KINDS:
        raise ValueError(
            f"unknown A2X_REGISTRY_DB_KIND={db_kind!r}; "
            f"accepted values: {', '.join(_VALID_DB_KINDS)}"
        )

    return RuntimeConfig(
        mode=mode, bind=bind, port=port,
        ha_members=ha_members, db_kind=db_kind,
    )


def _build_parser() -> argparse.ArgumentParser:
    """Build the serve-mode CLI parser.

    Deliberately omits ``--host``: the listen address is env-driven
    (``A2X_REGISTRY_BIND``) so it can be supplied via ``registry.env``
    without touching the systemd ``ExecStart`` line. ``--port`` is kept as
    a dev convenience; when both are set, the env var wins (parsed in
    ``parse_runtime_config`` before this parser runs).
    """
    parser = argparse.ArgumentParser(
        prog="a2x-registry",
        description="A2X Registry — backend API server",
    )
    parser.add_argument(
        "--port", type=int, default=None,
        help="Port override (A2X_REGISTRY_PORT env wins if set; default 8000)",
    )
    parser.add_argument(
        "--reload", action="store_true", default=False,
        help="Enable auto-reload (dev only)",
    )
    parser.add_argument(
        "--keep-alive", type=int, default=75,
        help="HTTP keep-alive timeout in seconds (default 75; must be >= heartbeat interval)",
    )
    return parser


def _serve(argv) -> None:
    """Start the uvicorn server using env-driven config.

    ``--port`` on the CLI is a fallback only; ``A2X_REGISTRY_PORT`` env
    var takes precedence so ``registry.env`` remains the single source of
    truth for deployment.
    """
    parser = _build_parser()
    args = parser.parse_args(argv)

    cfg = parse_runtime_config()
    # CLI --port is a dev fallback; env var (already in cfg) wins when set.
    port = cfg.port if os.environ.get(_ENV_PORT, "").strip() else (args.port or cfg.port)

    print(f"\n  A2X Registry")
    print(f"  http://{cfg.bind}:{port}")
    print(f"  Docs: http://{cfg.bind}:{port}/docs\n")

    import uvicorn
    uvicorn.run(
        "a2x_registry.backend.app:app",
        host=cfg.bind,
        port=port,
        reload=args.reload,
        timeout_keep_alive=args.keep_alive,
    )


def main() -> None:
    """Top-level dispatch: route ``auth`` / ``cluster`` subcommands, else serve."""
    if len(sys.argv) >= 2 and sys.argv[1] == "auth":
        from a2x_registry.auth.cli import main as auth_main
        sys.exit(auth_main(sys.argv[2:]))
    if len(sys.argv) >= 2 and sys.argv[1] == "cluster":
        from a2x_registry.cluster.cli import main as cluster_main
        sys.exit(cluster_main(sys.argv[2:]))
    _serve(sys.argv[1:])


if __name__ == "__main__":
    main()
