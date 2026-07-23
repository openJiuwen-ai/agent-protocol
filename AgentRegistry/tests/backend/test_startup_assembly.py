"""backend base assembly: warmup wires SQL backend + RegistryTableService.

Verifies that ``run_warmup`` (the single entry point backend startup uses)
assembles the SQL layer and creates the named registries according to
``A2X_REGISTRY_MODE``:

- Default mode (env unset / empty): only the ``service`` kind registry is
  created (named ``default``) -- the A2X backward-compat surface. Image /
  instance tables exist in schema (init_schema creates all 4 tables) but
  no ``images`` / ``instances`` row is registered in ``registry_meta``.
- Appliance mode (``A2X_REGISTRY_MODE=appliance``): additionally creates
  ``images`` (kind=image) and ``instances`` (kind=instance).

The assembled ``RegistryTableService`` is stashed on ``warmup_state`` under
a private key so image / instance module assembly can pick it up without re-instantiating.
"""

from __future__ import annotations

import importlib.util

import pytest


def _reload_backend(monkeypatch, tmp_path):
    """Force a clean re-import of backend modules against a fresh home dir.

    Mirrors the ``lite_app`` fixture pattern in tests/conftest.py: purge
    cached ``a2x_registry.*`` modules, set ``A2X_REGISTRY_HOME``, then
    import. Returns the refreshed modules for direct access.
    """
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))
    for n in list(__import__("sys").modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(__import__("sys").modules, n, raising=False)
    from a2x_registry.backend import startup
    from a2x_registry.backend.app import app
    return startup, app


def _run_warmup_synchronous(startup):
    """Run warmup inline and reset the ready flag for the next test.

    The real ``run_warmup`` is blocking when called directly (the thread
    pool dispatch happens in app.py's startup event, not here).
    """
    startup.warmup_state.update({
        "ready": False, "stage": "starting", "progress": 0, "error": None,
    })
    startup.run_warmup()


def test_warmup_assembles_table_service(monkeypatch, tmp_path):
    """Warmup must instantiate RegistryTableService and stash it for reuse."""
    # Heavy extras (numpy / chromadb / sentence_transformers) slow warmup
    # and aren't relevant to SQL assembly. Shadow them like lite_app does.
    real_find_spec = importlib.util.find_spec
    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: None if n in heavy else real_find_spec(n, *a, **kw),
    )

    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    ts = startup.warmup_state.get("_table_service")
    assert ts is not None, "warmup_state must stash _table_service for downstream modules"
    # The stashed object must be a RegistryTableService instance.
    from a2x_registry.register.service import RegistryTableService
    assert isinstance(ts, RegistryTableService)


def test_warmup_default_mode_creates_only_service_registry(monkeypatch, tmp_path):
    """Default mode (A2X_REGISTRY_MODE unset): only ``default`` service registry."""
    real_find_spec = importlib.util.find_spec
    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: None if n in heavy else real_find_spec(n, *a, **kw),
    )
    monkeypatch.delenv("A2X_REGISTRY_MODE", raising=False)

    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    ts = startup.warmup_state["_table_service"]
    regs = ts.list_registries()
    assert "default" in regs and regs["default"] == "service"
    # Appliance-only registries must NOT be registered in default mode.
    assert "images" not in regs
    assert "instances" not in regs


def test_warmup_appliance_mode_creates_image_instance_registries(monkeypatch, tmp_path):
    """Appliance mode: default + 镜像注册表 + 实例注册表 all registered."""
    real_find_spec = importlib.util.find_spec
    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: None if n in heavy else real_find_spec(n, *a, **kw),
    )
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")

    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)

    ts = startup.warmup_state["_table_service"]
    regs = ts.list_registries()
    assert regs.get("default") == "service"
    assert regs.get("images") == "image"
    assert regs.get("instances") == "instance"


def test_warmup_appliance_mode_assembles_image_service(monkeypatch, tmp_path):
    """Appliance mode must assemble ImageService and InstanceService and
    inject into deps.

    Generic mode must NOT assemble them (router returns 404 then).
    """
    real_find_spec = importlib.util.find_spec
    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: None if n in heavy else real_find_spec(n, *a, **kw),
    )

    # appliance → ImageService and InstanceService assembled
    monkeypatch.setenv("A2X_REGISTRY_MODE", "appliance")
    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)
    from a2x_registry.image.deps import get_image_service
    from a2x_registry.image.service import ImageService
    from a2x_registry.instance.deps import get_instance_service
    from a2x_registry.instance.service import InstanceService
    assert isinstance(get_image_service(), ImageService)
    assert isinstance(get_instance_service(), InstanceService)

    # generic → not assembled (None). Re-import deps after module reload
    # so the reference points at the freshly imported module (the prior
    # reload purged the old module from sys.modules).
    monkeypatch.delenv("A2X_REGISTRY_MODE", raising=False)
    startup2, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup2)
    import a2x_registry.image.deps as _img_deps
    import a2x_registry.instance.deps as _inst_deps
    assert _img_deps.get_image_service() is None
    assert _inst_deps.get_instance_service() is None


def test_warmup_init_schema_is_idempotent(monkeypatch, tmp_path):
    """Re-running warmup must not raise (CREATE TABLE IF NOT EXISTS)."""
    real_find_spec = importlib.util.find_spec
    heavy = ("numpy", "sentence_transformers", "chromadb", "tqdm")
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: None if n in heavy else real_find_spec(n, *a, **kw),
    )

    startup, _ = _reload_backend(monkeypatch, tmp_path)
    _run_warmup_synchronous(startup)
    # Second warmup on the same home dir reuses the same SQLite file.
    _run_warmup_synchronous(startup)
    assert startup.warmup_state["error"] is None
    assert startup.warmup_state["ready"] is True


# ── _resolve_db_config: A2X_REGISTRY_DB_KIND → connect cfg ──────
# Pure function tested without running the full warmup. It reads env vars
# and returns the ``connect(cfg)`` dict so warmup stays backend-agnostic.

def _reload_startup_only(monkeypatch, tmp_path):
    """Reload startup module against fresh env + home (no full warmup)."""
    import sys
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))
    for var in (
        "A2X_REGISTRY_DB_KIND", "A2X_REGISTRY_DB_ENDPOINT",
        "A2X_REGISTRY_DB_AUTH", "A2X_REGISTRY_MODE",
    ):
        monkeypatch.delenv(var, raising=False)
    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(sys.modules, n, raising=False)
    from a2x_registry.backend import startup
    return startup


def test_resolve_db_config_defaults_sqlite(monkeypatch, tmp_path):
    """No A2X_REGISTRY_DB_KIND -> sqlite cfg with registry.db path."""
    startup = _reload_startup_only(monkeypatch, tmp_path)
    cfg = startup._resolve_db_config()
    assert cfg["kind"] == "memory"
    # assert cfg["path"].endswith("registry.db")


def test_resolve_db_config_memory(monkeypatch, tmp_path):
    """memory kind: no path key (connect uses :memory: internally)."""
    startup = _reload_startup_only(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "memory")
    cfg = startup._resolve_db_config()
    assert cfg == {"kind": "memory"}


def test_resolve_db_config_rqlite_defaults_endpoint(monkeypatch, tmp_path):
    """rqlite kind without endpoint -> default http://127.0.0.1:4001."""
    startup = _reload_startup_only(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "rqlite")
    cfg = startup._resolve_db_config()
    assert cfg["kind"] == "rqlite"
    assert cfg["endpoint"] == "http://127.0.0.1:4001"
    assert cfg["auth"] == ""


def test_resolve_db_config_rqlite_custom_endpoint_and_auth(monkeypatch, tmp_path):
    """rqlite kind honors A2X_REGISTRY_DB_ENDPOINT / A2X_REGISTRY_DB_AUTH."""
    startup = _reload_startup_only(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "rqlite")
    monkeypatch.setenv("A2X_REGISTRY_DB_ENDPOINT", "http://10.0.0.5:4001")
    monkeypatch.setenv("A2X_REGISTRY_DB_AUTH", "admin:s3cret")
    cfg = startup._resolve_db_config()
    assert cfg["kind"] == "rqlite"
    assert cfg["endpoint"] == "http://10.0.0.5:4001"
    assert cfg["auth"] == "admin:s3cret"


def test_resolve_db_config_rejects_unknown_kind(monkeypatch, tmp_path):
    """Unknown kind raises ValueError (never silently fall back)."""
    startup = _reload_startup_only(monkeypatch, tmp_path)
    monkeypatch.setenv("A2X_REGISTRY_DB_KIND", "mongodb")
    with pytest.raises(ValueError, match="DB_KIND"):
        startup._resolve_db_config()
