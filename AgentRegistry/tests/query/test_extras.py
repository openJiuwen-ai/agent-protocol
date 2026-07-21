"""Query (extras feature flag) contract tests.

These cover the install-model machinery that the query feature stands
on: ``feature_flags.has()`` / ``feature_flags.require()`` and the
FastAPI app's ability to boot in lite mode (so the SDK paths exposed
by query — e.g. /api/datasets/embedding-models — stay reachable).
"""

from __future__ import annotations

import importlib

import pytest


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


def test_feature_flags_full():
    """In full mode has() returns True and require() is a no-op."""
    pytest.importorskip("numpy")
    from a2x_registry.common import feature_flags
    assert feature_flags.has("vector") is True
    assert feature_flags.has("evaluation") is True
    feature_flags.require("vector")
    feature_flags.require("evaluation")
