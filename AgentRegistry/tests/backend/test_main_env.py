"""CLI env handling: ``registry.env`` read by ``backend/__main__.py``.

730 contract :
- CLI must NOT take a ``--host`` flag; the listen address comes from env.
- ``A2X_REGISTRY_BIND`` empty/missing -> ``127.0.0.1`` (localhost only).
- ``A2X_REGISTRY_BIND=0.0.0.0`` is rejected (security: no wildcard bind).
- ``A2X_REGISTRY_PORT`` overrides the default 8000.
- ``A2X_REGISTRY_MODE`` selects generic vs appliance schema bootstrap.
- ``A2X_REGISTRY_HA_MEMBERS`` must be empty in 730 (single-node SQLite);
  a non-empty value is a later release and is rejected now.

These tests target the pure env-parsing layer (no uvicorn start) so they
stay fast and side-effect-free.
"""

from __future__ import annotations

import importlib
import importlib.util
import sys

import pytest


def _reload_main(monkeypatch, tmp_path):
    """Reload ``a2x_registry.backend.__main__`` against fresh env + home.

    Returns the freshly imported module so its top-level helpers can be
    tested without triggering uvicorn.
    """
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))
    # Clear all env vars under test so each case starts from a known state.
    for var in (
        "A2X_REGISTRY_MODE", "A2X_REGISTRY_BIND",
        "A2X_REGISTRY_PORT", "A2X_REGISTRY_HA_MEMBERS",
        "A2X_REGISTRY_DB_KIND",
    ):
        monkeypatch.delenv(var, raising=False)
    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(sys.modules, n, raising=False)
    import a2x_registry.backend.__main__ as main_mod
    return main_mod


def test_parse_bind_defaults_to_localhost(monkeypatch, tmp_path):
    """No A2X_REGISTRY_BIND -> 127.0.0.1 (loopback only, never wildcard)."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    cfg = main_mod.parse_runtime_config()
    assert cfg.bind == "127.0.0.1"


def test_parse_bind_from_env(monkeypatch, tmp_path):
    """A2X_REGISTRY_BIND is honored when set to a concrete IP."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_BIND", "10.0.0.5")
    cfg = main_mod.parse_runtime_config()
    assert cfg.bind == "10.0.0.5"


def test_parse_bind_rejects_wildcard(monkeypatch, tmp_path):
    """0.0.0.0 is forbidden -- 730 binds to a specific interface or loopback."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_BIND", "0.0.0.0")
    with pytest.raises(ValueError, match="0.0.0.0"):
        main_mod.parse_runtime_config()


def test_parse_port_defaults_to_8000(monkeypatch, tmp_path):
    main_mod = _reload_main(monkeypatch, tmp_path)
    cfg = main_mod.parse_runtime_config()
    assert cfg.port == 8000


def test_parse_port_from_env(monkeypatch, tmp_path):
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_PORT", "9000")
    cfg = main_mod.parse_runtime_config()
    assert cfg.port == 9000


def test_parse_port_invalid_raises(monkeypatch, tmp_path):
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_PORT", "not-a-number")
    with pytest.raises(ValueError, match="PORT"):
        main_mod.parse_runtime_config()


def test_parse_mode_defaults_empty(monkeypatch, tmp_path):
    """No A2X_REGISTRY_MODE -> empty string (generic service-only mode)."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    cfg = main_mod.parse_runtime_config()
    assert cfg.mode == ""


def test_parse_mode_appliance(monkeypatch, tmp_path):
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    cfg = main_mod.parse_runtime_config()
    assert cfg.mode == "appliance"


def test_parse_mode_unknown_rejected(monkeypatch, tmp_path):
    """Only '' (generic) and 'appliance' are valid in 730."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_MODE", "cluster")
    with pytest.raises(ValueError, match="MODE"):
        main_mod.parse_runtime_config()


def test_parse_ha_members_must_be_empty_in_730(monkeypatch, tmp_path):
    """Non-empty HA members indicate a later (rqlite) release; 730 rejects."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_HA_MEMBERS", "node1,node2")
    with pytest.raises(ValueError, match="HA"):
        main_mod.parse_runtime_config()


def test_parse_ha_members_empty_ok(monkeypatch, tmp_path):
    """Empty / unset HA members is the 730 single-node contract."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    cfg = main_mod.parse_runtime_config()
    assert cfg.ha_members == ()


def test_cli_no_host_flag(monkeypatch, tmp_path):
    """The argparse parser must NOT accept --host (listen addr is env-driven)."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    parser = main_mod._build_parser()
    # --port is kept (backward-compat convenience, env wins if both set).
    args = parser.parse_args(["--port", "9000"])
    assert args.port == 9000
    # --host must not be a known argument.
    with pytest.raises(SystemExit):
        parser.parse_args(["--host", "1.2.3.4"])


# ── A2X_REGISTRY_DB_KIND ────────────────────────────────────────
# 730 default is sqlite (single-node, file-persisted). ``memory`` is a
# debug-only in-process backend. ``rqlite`` selects the Raft-replicated
# backend (endpoint/auth come from separate env vars, read in startup.py).
# Any other value is rejected so a typo doesn't silently fall back.

def test_parse_db_kind_defaults_sqlite(monkeypatch, tmp_path):
    """No A2X_REGISTRY_DB_KIND -> 'sqlite' (production single-node default)."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    cfg = main_mod.parse_runtime_config()
    assert cfg.db_kind == "sqlite"


def test_parse_db_kind_empty_defaults_sqlite(monkeypatch, tmp_path):
    """Empty A2X_REGISTRY_DB_KIND -> 'sqlite' (treated as unset)."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "")
    cfg = main_mod.parse_runtime_config()
    assert cfg.db_kind == "sqlite"


def test_parse_db_kind_sqlite_explicit(monkeypatch, tmp_path):
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "sqlite")
    cfg = main_mod.parse_runtime_config()
    assert cfg.db_kind == "sqlite"


def test_parse_db_kind_memory(monkeypatch, tmp_path):
    """memory kind: debug-only in-process backend, no file persistence."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "memory")
    cfg = main_mod.parse_runtime_config()
    assert cfg.db_kind == "memory"


def test_parse_db_kind_rqlite(monkeypatch, tmp_path):
    """rqlite kind: Raft-replicated backend (endpoint read in startup.py)."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "rqlite")
    cfg = main_mod.parse_runtime_config()
    assert cfg.db_kind == "rqlite"


def test_parse_db_kind_unknown_rejected(monkeypatch, tmp_path):
    """Unknown kind is rejected so a typo doesn't silently fall back to sqlite."""
    main_mod = _reload_main(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "mongodb")
    with pytest.raises(ValueError, match="DB_KIND"):
        main_mod.parse_runtime_config()
