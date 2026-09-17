"""Query (search) feature tests.

Covers the A2X search surface:
- POST /api/search           — sync search; ``method="vector"`` gates on
                               ``[vector]`` extras (503 in lite). A2X /
                               Traditional methods are pure-LLM and run on
                               lite as long as ``llm_apikey.json`` exists.
- POST /api/search/judge     — relevance judge; pure-LLM, no extras gating.
                               Returns 503 with ``reason="llm_not_configured"``
                               when ``llm_apikey.json`` is absent.
- WS   /api/search/ws        — streaming search (lite delivers install hint
                               for vector method).

Each "503 in lite" assertion below is checking the *structured* error body
so SDK users see an actionable copy-pasteable hint, not a stack trace.
"""

from __future__ import annotations


def test_search_returns_503(lite_app, dataset):
    r = lite_app.post(
        "/api/search",
        json={"query": "x", "method": "vector", "dataset": dataset, "top_k": 3},
    )
    assert r.status_code == 503
    body = r.json()
    assert body["feature"] == "vector"
    assert body["extras"] == "vector"
    assert "pip install 'a2x-registry[vector]'" in body["detail"]


def test_search_judge_returns_503_when_llm_unconfigured(lite_app):
    """Judge is pure-LLM and un-gated from [vector], but it still needs
    ``llm_apikey.json``. In the lite test fixture the env var
    ``A2X_REGISTRY_HOME`` points at an empty tmp dir, so the LLM client
    raises ``LLMNotConfiguredError`` which the app handler renders as 503
    with ``reason: "llm_not_configured"`` + setup instructions in ``detail``.
    """
    r = lite_app.post(
        "/api/search/judge",
        json={"query": "x", "services": []},
    )
    assert r.status_code == 503
    body = r.json()
    assert body["reason"] == "llm_not_configured"
    assert "llm_apikey.json" in body["detail"]


def test_search_ws_returns_install_hint(lite_app, dataset):
    """WebSocket: server accepts, then sends an error JSON with the hint."""
    with lite_app.websocket_connect("/api/search/ws") as ws:
        ws.send_json({
            "query": "x", "method": "vector",
            "dataset": dataset, "top_k": 3,
        })
        msg = ws.receive_json()
        assert msg["type"] == "error"
        assert "pip install 'a2x-registry[vector]'" in msg["message"]


def test_search_route_passes_gate_full(full_app):
    """In full mode the search gate is a no-op and the route is mounted."""
    from a2x_registry.common import feature_flags
    feature_flags.require("vector")  # must not raise in full mode

    paths = [r.path for r in full_app.app.routes if hasattr(r, "path")]
    assert "/api/search" in paths
    assert "/api/search/judge" in paths
