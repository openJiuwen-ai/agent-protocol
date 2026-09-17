"""Shared fixtures for the per-feature test folders.

The agent-protocol test suite is split into one folder per outward-facing
feature: query / registered / deregistered / update / reserve. They all
share the same install-model fixtures:

- ``lite_app``      — FastAPI TestClient with the heavy extras shadowed,
                      so the lite (default ``pip install``) contract can
                      be exercised even when ``[vector]`` is locally
                      installed.
- ``full_app``      — FastAPI TestClient in full mode (no shadowing).
                      Skipped if numpy isn't importable.
- ``lite_env``      — monkeypatch fixture that simulates the heavy extras
                      being absent, for import-time contract tests.
- ``dataset``       — a fresh dataset name per test, auto-deleted on
                      teardown. Depends on ``lite_app``.
- ``agent_card``    — factory fixture for valid A2A Agent Cards
                      (call it inside a test to build a card dict).

Tests that require numpy/[vector] extras call ``pytest.importorskip("numpy")``
at the top of the test body — see test_embedding.py / test_extras.py.
"""

from __future__ import annotations

import importlib
import importlib.util
import os
import sys
import uuid

import pytest


_HEAVY = ("numpy", "sentence_transformers", "chromadb", "tqdm")


# ── install-mode fixtures ────────────────────────────────────────────────────


@pytest.fixture
def lite_app(tmp_path, monkeypatch):
    """Boot the FastAPI app inside a lite-simulated env (no heavy extras).

    Function-scoped: ``feature_flags.has()`` re-probes ``find_spec`` on
    every request, so the patch must stay live for the whole client
    lifetime. ``monkeypatch`` restores ``find_spec`` and ``A2X_REGISTRY_HOME``
    automatically on teardown — including if app boot / warmup raises
    mid-setup — so a flaky test cannot poison the rest of the session.
    """
    real_find_spec = importlib.util.find_spec

    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda n, *a, **kw: (
            None if n in _HEAVY else real_find_spec(n, *a, **kw)
        ),
    )
    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))

    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(sys.modules, n, raising=False)

    from a2x_registry.backend.app import app
    from a2x_registry.backend.startup import run_warmup

    run_warmup()

    from fastapi.testclient import TestClient
    yield TestClient(app)


@pytest.fixture
def full_app(tmp_path, monkeypatch):
    """Boot the full-mode app (no shadowing); skipped if numpy is absent.

    Uses ``monkeypatch.delitem`` (not raw ``del``) for symmetry with
    ``lite_app`` — keeps the two fixtures' cleanup model identical.
    """
    if importlib.util.find_spec("numpy") is None:
        pytest.skip("[vector] extras not installed; full-mode fixture skipped")

    monkeypatch.setenv("A2X_REGISTRY_HOME", str(tmp_path))
    for n in list(sys.modules):
        if n.startswith("a2x_registry"):
            monkeypatch.delitem(sys.modules, n, raising=False)
    from a2x_registry.backend.app import app
    from a2x_registry.backend.startup import run_warmup
    run_warmup()
    from fastapi.testclient import TestClient
    return TestClient(app)


@pytest.fixture
def lite_env(monkeypatch):
    """Simulate the heavy extras as not installed, for import-time tests."""
    to_drop = [
        n for n in list(sys.modules)
        if n.startswith("a2x_registry") or n in _HEAVY
    ]
    for name in to_drop:
        monkeypatch.delitem(sys.modules, name, raising=False)

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


# ── data helpers ─────────────────────────────────────────────────────────────


def make_agent_card(name: str = "agent-1") -> dict:
    """Build a minimal but valid A2A Agent Card for tests."""
    return {
        "name": name,
        "description": "tester",
        "url": "http://example.invalid",
        "version": "1.0",
        "protocolVersion": "0.0.1",
        "capabilities": {},
        "defaultInputModes": ["text/plain"],
        "defaultOutputModes": ["text/plain"],
        "skills": [
            {"id": "s", "name": "s", "description": "s", "tags": ["t"]}
        ],
    }


@pytest.fixture
def agent_card():
    """Factory for building Agent Card dicts inside tests."""
    return make_agent_card


@pytest.fixture
def dataset(lite_app):
    """A fresh dataset per test, auto-deleted on teardown."""
    name = "ds_" + uuid.uuid4().hex[:8]
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    yield name
    r = lite_app.delete(f"/api/datasets/{name}")
    assert r.status_code == 200, r.text


# ── extra summary line ───────────────────────────────────────────────────────


@pytest.hookimpl(hookwrapper=True, tryfirst=True)
def pytest_sessionfinish(session, exitstatus):
    """Print a final ``N tests, N passed`` separator after pytest's summary.

    Uses ``tryfirst=True`` so this wrapper is outermost — its post-yield
    code runs AFTER pytest's built-in summary_stats() line, making this
    line the actual last line of pytest output (good for screenshots).
    """
    yield
    reporter = session.config.pluginmanager.get_plugin("terminalreporter")
    if reporter is None:
        return
    stats = reporter.stats
    passed = len(stats.get("passed", []))
    failed = len(stats.get("failed", []))
    skipped = len(stats.get("skipped", []))
    errors = len(stats.get("error", []))
    total = passed + failed + skipped + errors
    reporter.write_sep("=", f"{total} tests, {passed} passed", bold=True)
