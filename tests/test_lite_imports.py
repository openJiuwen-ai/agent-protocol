"""Contract test: prove the lite install path stays importable.

The user-visible contract is that ``pip install a2x-registry`` (no extras)
produces a working SDK-facing backend, even though numpy / chromadb /
sentence_transformers / tqdm are absent. Rather than spinning up a fresh
venv (slow, fragile in CI), this test simulates the missing extras by
shadowing those module names in ``sys.modules`` so any real import resolves
to ``None`` and ``importlib.util.find_spec`` returns ``None``.

Run with::

    pytest tests/test_lite_imports.py -q
"""

from __future__ import annotations

import importlib
import sys
import types

import pytest


# Modules that are present in [vector] / [evaluation] extras and must be
# absent in lite. We replace them with sentinels so any code that *tries*
# to import them blows up loudly inside this test, but ``find_spec`` for
# the real names still returns ``None`` (we delete-then-shadow below).
_HEAVY = ("numpy", "sentence_transformers", "chromadb", "tqdm")


@pytest.fixture
def lite_env(monkeypatch):
    """Make the heavy extras look uninstalled for the duration of one test."""
    # Drop any cached import state from prior tests / sessions so the modules
    # we touch below are re-imported fresh against the simulated env.
    to_drop = [
        n for n in list(sys.modules)
        if n.startswith("a2x_registry") or n in _HEAVY
    ]
    for name in to_drop:
        monkeypatch.delitem(sys.modules, name, raising=False)

    # Block real imports of the heavy modules: make ``find_spec`` return
    # ``None`` and any actual ``import X`` raise ``ImportError``.
    real_find_spec = importlib.util.find_spec

    def fake_find_spec(name, *args, **kwargs):
        if name in _HEAVY:
            return None
        return real_find_spec(name, *args, **kwargs)

    monkeypatch.setattr(importlib.util, "find_spec", fake_find_spec)

    class _BlockedFinder:
        def find_spec(self, fullname, path=None, target=None):
            if fullname in _HEAVY or any(
                fullname.startswith(h + ".") for h in _HEAVY
            ):
                raise ImportError(
                    f"[lite test] {fullname} is simulated as not installed"
                )
            return None

    monkeypatch.setattr(sys, "meta_path", [_BlockedFinder(), *sys.meta_path])
    yield


def test_embedding_constants_lite_safe(lite_env):
    """The zero-dep submodule must import in lite and expose both literals."""
    mod = importlib.import_module(
        "a2x_registry.vector.utils.embedding_constants"
    )
    assert isinstance(mod.DEFAULT_EMBEDDING_MODEL, str)
    assert mod.DEFAULT_EMBEDDING_MODEL in mod.EMBEDDING_MODELS
    assert mod.DEFAULT_EMBEDDING_MODEL == "all-MiniLM-L6-v2"


def test_vector_utils_init_lite_safe(lite_env):
    """vector.utils package must initialize without touching heavy deps.

    PEP 562 ``__getattr__`` only loads ``EmbeddingModel`` / ``ChromaStore``
    on attribute access; bare ``import`` does not pull them in.
    """
    pkg = importlib.import_module("a2x_registry.vector.utils")
    # Constants are eagerly loaded.
    assert pkg.DEFAULT_EMBEDDING_MODEL == "all-MiniLM-L6-v2"
    # Heavy attributes are listed but only resolve on access.
    assert "EmbeddingModel" in pkg.__all__
    assert "ChromaStore" in pkg.__all__
    with pytest.raises(ImportError):
        pkg.EmbeddingModel  # accessing it triggers the heavy import


def test_register_service_lite_safe(lite_env):
    """register.service must import: lite SDK uses _ensure_dataset_initialized."""
    importlib.import_module("a2x_registry.register.service")


def test_backend_app_lite_safe(lite_env):
    """The FastAPI app must construct without [vector] extras installed."""
    app_mod = importlib.import_module("a2x_registry.backend.app")
    assert app_mod.app is not None


def test_feature_flags_reports_missing(lite_env):
    """has() returns False; require() raises with a copy-pasteable hint."""
    from a2x_registry.common import feature_flags
    from a2x_registry.common.errors import FeatureNotInstalledError

    assert feature_flags.has("vector") is False
    assert feature_flags.has("evaluation") is False
    with pytest.raises(FeatureNotInstalledError) as exc:
        feature_flags.require("vector")
    assert "pip install 'a2x-registry[vector]'" in str(exc.value)
    assert exc.value.feature == "vector"
    assert exc.value.extras == "vector"


# The legacy-path consistency check (constants resolve via three import
# paths) lives in tests/test_full_regression.py — it needs the real
# ``embedding.py`` (numpy + sentence_transformers loaded), which doesn't
# fit inside the lite-simulation fixture.
