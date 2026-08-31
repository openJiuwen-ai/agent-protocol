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
    A2X_REGISTRY_DB_KIND     empty -> sqlite | "memory" (debug) | "etcd" (appliance only)
    A2X_REGISTRY_DB_ENDPOINT http(s):// URL — required when DB_KIND=etcd
    A2X_REGISTRY_ETCD_NAMESPACE         key prefix; default a2x-registry
    A2X_REGISTRY_ETCD_TLS_CA / _CERT / _KEY — all set -> mTLS, all empty -> no auth
    A2X_REGISTRY_TLS_CERTFILE / _KEYFILE / _CA_CERTS
                             all empty -> http ; all three set -> mutual TLS
    A2X_REGISTRY_LOG_DIR     empty -> stderr only (journalctl) ; else daily files here
                             (a2x-registry.log for today; rotated -> a2x-registry-YYYY-MM-DD.log.gz)
    A2X_REGISTRY_LOG_RETENTION_DAYS
                             daily-rotated .gz files to keep (default 7)

Auth admin subcommands (no server needed):
    a2x-registry auth init                       # bootstrap first admin key
    a2x-registry auth reset-admin --confirm      # rotate the bootstrap admin

Cluster subcommands (distributed sync):
    a2x-registry cluster init                    # generate node id (opt-in)
    a2x-registry cluster status                  # show sync state
"""

from __future__ import annotations

import argparse
import logging
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Tuple

from a2x_registry.backend.startup import VALID_DB_KINDS
from a2x_registry.common.log import DailyCompressedFileHandler


# ── env var names ────────────────────────────────────────────────────────
_ENV_MODE = "A2X_REGISTRY_MODE"
_ENV_BIND = "A2X_REGISTRY_BIND"
_ENV_PORT = "A2X_REGISTRY_PORT"
_ENV_HA_MEMBERS = "A2X_REGISTRY_HA_MEMBERS"
_ENV_DB_KIND = "A2X_REGISTRY_DB_KIND"
_ENV_TLS_CERTFILE = "A2X_REGISTRY_TLS_CERTFILE"
_ENV_TLS_KEYFILE = "A2X_REGISTRY_TLS_KEYFILE"
_ENV_TLS_CA_CERTS = "A2X_REGISTRY_TLS_CA_CERTS"
_ENV_LOG_DIR = "A2X_REGISTRY_LOG_DIR"
_ENV_LOG_RETENTION_DAYS = "A2X_REGISTRY_LOG_RETENTION_DAYS"

_VALID_MODES = ("", "appliance")
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
      (single-node only). Non-empty indicates a later distributed
      (etcd) release.
    - ``db_kind``: storage backend kind - ``sqlite`` (production single-node,
      file-persisted) or ``memory`` (debug only, in-process, lost on exit).
      ``etcd`` (distributed shared-store) is appliance-only; endpoint /
      namespace / TLS for it are validated in ``startup._resolve_db_config``.
      Empty env var defaults to ``sqlite``.
    - ``tls_certfile`` / ``tls_keyfile`` / ``tls_ca_certs``: mTLS material.
      All empty -> plain http. All three set -> mutual TLS (the server
      requires + verifies the caller's client cert). Partial config is
      rejected in ``parse_runtime_config``.
    - ``log_dir``: directory for daily-rotating log files; empty disables
      file logging (logs still go to stderr, so journalctl keeps working).
      The current day's file is fixed as ``a2x-registry.log``; rotated days
      are archived as ``a2x-registry-YYYY-MM-DD.log.gz``.
    - ``log_retention_days``: how many daily-rotated files to keep.
    """

    mode: str
    bind: str
    port: int
    ha_members: Tuple[str, ...]
    db_kind: str
    tls_certfile: str = ""
    tls_keyfile: str = ""
    tls_ca_certs: str = ""
    log_dir: str = ""
    log_retention_days: int = 7


def parse_runtime_config() -> RuntimeConfig:
    """Parse runtime config from environment variables.

    Raises ``ValueError`` on:
      - unknown ``A2X_REGISTRY_MODE`` (only "" / "appliance" valid)
      - ``A2X_REGISTRY_BIND=0.0.0.0`` (wildcard forbidden)
      - noninteger ``A2X_REGISTRY_PORT``
      - non-empty ``A2X_REGISTRY_HA_MEMBERS`` (single-node only)
      - unknown ``A2X_REGISTRY_DB_KIND`` (only sqlite / memory)
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
            "single-node; distributed etcd backend is a later release"
        )

    db_kind = os.environ.get(_ENV_DB_KIND, "").strip() or "sqlite"
    if db_kind not in VALID_DB_KINDS:
        raise ValueError(
            f"unknown A2X_REGISTRY_DB_KIND={db_kind!r}; "
            f"accepted values: {', '.join(VALID_DB_KINDS)}"
        )
    if db_kind == "etcd" and mode != "appliance":
        raise ValueError(
            "A2X_REGISTRY_DB_KIND=etcd requires A2X_REGISTRY_MODE=appliance"
        )

    tls_certfile = os.environ.get(_ENV_TLS_CERTFILE, "").strip()
    tls_keyfile = os.environ.get(_ENV_TLS_KEYFILE, "").strip()
    tls_ca_certs = os.environ.get(_ENV_TLS_CA_CERTS, "").strip()
    _tls_set = (bool(tls_certfile), bool(tls_keyfile), bool(tls_ca_certs))
    if any(_tls_set) and not all(_tls_set):
        raise ValueError(
            "A2X_REGISTRY_TLS_CERTFILE / _KEYFILE / _CA_CERTS must all be set "
            "together (enables mutual TLS) or all empty (plain http)"
        )
    for _name, _path in (
        (_ENV_TLS_CERTFILE, tls_certfile),
        (_ENV_TLS_KEYFILE, tls_keyfile),
        (_ENV_TLS_CA_CERTS, tls_ca_certs),
    ):
        if _path and not os.path.isfile(_path):
            raise ValueError(f"{_name}={_path!r} file not found")

    log_dir = os.environ.get(_ENV_LOG_DIR, "").strip()
    retention_raw = os.environ.get(_ENV_LOG_RETENTION_DAYS, "").strip() or "7"
    try:
        log_retention_days = int(retention_raw)
    except ValueError as exc:
        raise ValueError(
            f"A2X_REGISTRY_LOG_RETENTION_DAYS={retention_raw!r} is not an integer"
        ) from exc
    if log_retention_days < 1:
        raise ValueError("A2X_REGISTRY_LOG_RETENTION_DAYS must be >= 1")

    return RuntimeConfig(
        mode=mode, bind=bind, port=port,
        ha_members=ha_members, db_kind=db_kind,
        tls_certfile=tls_certfile, tls_keyfile=tls_keyfile,
        tls_ca_certs=tls_ca_certs,
        log_dir=log_dir,
        log_retention_days=log_retention_days,
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


def _configure_logging(cfg: RuntimeConfig) -> None:
    """Route all logs (uvicorn + app) to stderr AND a daily-rotating file.

    The stderr handler keeps systemd/journalctl working exactly as before.
    When ``cfg.log_dir`` is set, a ``DailyCompressedFileHandler`` writes the
    fixed-name file ``a2x-registry.log`` for the current day, gzips the
    finished day's file to ``a2x-registry-YYYY-MM-DD.log.gz`` on rotation,
    and keeps ``log_retention_days`` old compressed files.
    ``uvicorn.run(log_config=None)`` leaves this root-level
    config in place, so uvicorn's own loggers (``uvicorn`` / ``uvicorn.error``
    / ``uvicorn.access``) propagate here and land in both sinks.
    """
    handlers: list = [logging.StreamHandler(sys.stderr)]
    if cfg.log_dir:
        log_dir = Path(cfg.log_dir)
        log_dir.mkdir(parents=True, exist_ok=True)
        handlers.append(
            DailyCompressedFileHandler(log_dir, "a2x-registry", cfg.log_retention_days)
        )
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        handlers=handlers,
        force=True,
    )


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

    scheme = "https" if cfg.tls_certfile else "http"
    print(f"\n  A2X Registry")
    print(f"  {scheme}://{cfg.bind}:{port}")
    print(f"  Docs: {scheme}://{cfg.bind}:{port}/docs\n")

    ssl_kwargs = {}
    if cfg.tls_certfile:
        import ssl
        # Mutual TLS: require + verify the caller's client certificate.
        ssl_kwargs = dict(
            ssl_certfile=cfg.tls_certfile,
            ssl_keyfile=cfg.tls_keyfile,
            ssl_ca_certs=cfg.tls_ca_certs,
            ssl_cert_reqs=ssl.CERT_REQUIRED,
        )

    import uvicorn
    _configure_logging(cfg)
    uvicorn.run(
        "a2x_registry.backend.app:app",
        host=cfg.bind,
        port=port,
        reload=args.reload,
        timeout_keep_alive=args.keep_alive,
        log_config=None,  # keep our root-level config (stderr + optional file)
        **ssl_kwargs,
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
