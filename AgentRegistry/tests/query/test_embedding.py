"""Query (embedding) feature tests.

Embedding configuration is a query-time concern: it determines how
services are vectorized for the ``vector`` and ``a2x`` search methods.
The constants module is intentionally zero-dependency so the SDK can
read default embedding names without pulling in numpy.

Covers:
- GET /api/datasets/embedding-models                      — lite-safe
- ``a2x_registry.vector.utils.embedding_constants``       — lite-safe import
- ``a2x_registry.vector.utils`` package                   — lite-safe import
                                                            with PEP 562
                                                            lazy heavy attrs
- The same constant exposed by all three legacy import paths in full
"""

from __future__ import annotations

import importlib

import pytest


def test_embedding_models_lite_safe(lite_app):
    """Static metadata route must return 200 without [vector] extras."""
    r = lite_app.get("/api/datasets/embedding-models")
    assert r.status_code == 200
    models = r.json()["models"]
    assert "all-MiniLM-L6-v2" in models


def test_embedding_constants_lite_safe(lite_env):
    """The zero-dep submodule must import in lite and expose both literals."""
    mod = importlib.import_module(
        "a2x_registry.vector.utils.embedding_constants"
    )
    assert isinstance(mod.DEFAULT_EMBEDDING_MODEL, str)
    assert mod.DEFAULT_EMBEDDING_MODEL in mod.EMBEDDING_MODELS
    assert mod.DEFAULT_EMBEDDING_MODEL == "all-MiniLM-L6-v2"


def test_vector_utils_init_lite_safe(lite_env):
    """vector.utils package initializes without touching heavy deps."""
    pkg = importlib.import_module("a2x_registry.vector.utils")
    assert pkg.DEFAULT_EMBEDDING_MODEL == "all-MiniLM-L6-v2"
    assert "EmbeddingModel" in pkg.__all__
    assert "ChromaStore" in pkg.__all__
    with pytest.raises(ImportError):
        pkg.EmbeddingModel  # accessing it triggers the heavy import


def test_constants_three_legacy_paths_full():
    """In full mode, the same constant resolves through all three paths."""
    pytest.importorskip("numpy")
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


def test_pep562_resolves_heavy_classes_full():
    """PEP 562 ``__getattr__`` returns the real class in full mode."""
    pytest.importorskip("numpy")
    from a2x_registry.vector.utils import EmbeddingModel, ChromaStore
    from a2x_registry.vector.utils.embedding import EmbeddingModel as direct
    assert EmbeddingModel is direct
    assert ChromaStore.__name__ == "ChromaStore"


def test_dir_lists_lazy_attrs_full():
    """Tab-completion / introspection surfaces the heavy attrs in full mode."""
    pytest.importorskip("numpy")
    import a2x_registry.vector.utils as pkg
    listed = set(dir(pkg))
    assert {"EmbeddingModel", "ChromaStore", "metrics",
            "DEFAULT_EMBEDDING_MODEL", "EMBEDDING_MODELS"}.issubset(listed)
