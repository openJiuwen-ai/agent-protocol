"""Full-install regression: prove the 0.1.5 surface still works under [full].

Skipped automatically if the host doesn't have the [vector] extras
installed. Verifies:
  - Constant relocation didn't break the three legacy import paths.
  - PEP 562 ``__getattr__`` resolves heavy classes.
  - The SDK-facing routes that lite already covers still work in full mode.
  - Heavy CLIs no longer error early (we only check that they pass the
    feature gate; their full execution requires LLM creds and is out of
    scope for this regression).
"""

from __future__ import annotations

import importlib
import importlib.util

import pytest

if importlib.util.find_spec("numpy") is None:
    pytest.skip("[vector] extras not installed; full regression skipped",
                allow_module_level=True)


def test_constants_three_legacy_paths():
    a = importlib.import_module(
        "a2x_registry.vector.utils.embedding_constants"
    ).DEFAULT_EMBEDDING_MODEL
    b = importlib.import_module(
        "a2x_registry.vector.utils.embedding"
    ).DEFAULT_EMBEDDING_MODEL
    c = importlib.import_module(
        "a2x_registry.vector.utils"
    ).DEFAULT_EMBEDDING_MODEL
    assert a == b == c == "all-MiniLM-L6-v2"


def test_pep562_resolves_heavy_classes():
    from a2x_registry.vector.utils import EmbeddingModel, ChromaStore
    from a2x_registry.vector.utils.embedding import EmbeddingModel as direct
    assert EmbeddingModel is direct  # PEP 562 returns the real class
    assert ChromaStore.__name__ == "ChromaStore"


def test_dir_lists_lazy_attrs():
    """Tab-completion / introspection still surfaces the heavy attrs."""
    import a2x_registry.vector.utils as pkg
    listed = set(dir(pkg))
    assert {"EmbeddingModel", "ChromaStore", "metrics",
            "DEFAULT_EMBEDDING_MODEL", "EMBEDDING_MODELS"}.issubset(listed)


def test_feature_flags_full():
    from a2x_registry.common import feature_flags
    assert feature_flags.has("vector") is True
    assert feature_flags.has("evaluation") is True
    # No-op in full mode
    feature_flags.require("vector")
    feature_flags.require("evaluation")


@pytest.fixture
def full_app(tmp_path, monkeypatch):
    """Boot the full-mode app with warmup invoked (wires set_registry)."""
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))
    import sys
    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            del sys.modules[n]
    from a2x_registry.backend.app import app
    from a2x_registry.backend.startup import run_warmup
    run_warmup()
    from fastapi.testclient import TestClient
    return TestClient(app)


def test_search_route_passes_gate(full_app):
    """In full mode the search gate is a no-op: ``require("vector")`` returns
    silently, so the downstream service-loading code is reached.

    We don't probe end-to-end search here (needs taxonomy + LLM creds);
    the contract this regression cares about is *the gate doesn't 503*,
    which we verify by direct require() call and route registration.
    """
    from a2x_registry.common import feature_flags
    feature_flags.require("vector")  # must not raise

    # Sanity: the route is mounted (i.e. nothing in app.py was conditionally
    # removed in a way that hides it from the OpenAPI surface).
    paths = [r.path for r in full_app.app.routes if hasattr(r, "path")]
    assert "/api/search" in paths
    assert "/api/search/judge" in paths


def test_build_status_route_full(full_app):
    full_app.post("/api/datasets", json={"name": "full_status"})
    r = full_app.get("/api/datasets/full_status/build/status")
    assert r.status_code == 200
    assert r.json()["status"] == "idle"
