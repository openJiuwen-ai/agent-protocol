"""Regression: GET /api/providers must answer at the no-slash path.

Bug (gitcode agent-protocol #UI "ui界面调用不存在的接口"): the frontend and
``docs/backend_api.md`` both use ``GET /api/providers`` (no trailing slash),
but the route was declared ``@router.get("/")`` under prefix
``/api/providers`` — i.e. it only existed at ``/api/providers/``. In
production the SPA is mounted at ``/`` and swallows ``/api/providers``
before FastAPI's trailing-slash redirect can fire, so the call 404s.

The fix declares the list route as ``@router.get("")`` so the canonical
no-slash path resolves directly (no 307). These tests pin that contract.
"""

from __future__ import annotations

import json

from a2x_registry.common.paths import llm_apikey_path


def _write_apikey():
    """Drop a minimal llm_apikey.json at the resolved path (under tmp HOME)."""
    path = llm_apikey_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "providers": [
                    {"name": "deepseek", "base_url": "x", "model": "deepseek-chat",
                     "api_keys": ["sk-test"]},
                    {"name": "aliyun", "base_url": "y", "model": "deepseek-v3.2",
                     "api_keys": ["sk-test"]},
                ]
            }
        ),
        encoding="utf-8",
    )


def test_list_providers_resolves_without_trailing_slash(lite_app):
    """The no-slash path must return 200 *directly* — no 307 redirect.

    ``follow_redirects=False`` is the crux: on the buggy ``@router.get("/")``
    this path only existed at ``/api/providers/`` so the no-slash request
    came back as a 307. Asserting 200-without-redirect locks in the fix.
    """
    _write_apikey()

    r = lite_app.get("/api/providers", follow_redirects=False)
    assert r.status_code == 200, r.text

    body = r.json()
    assert "providers" in body and "current" in body
    names = [p["name"] for p in body["providers"]]
    assert names == ["deepseek", "aliyun"]
    assert all({"name", "model"} <= set(p) for p in body["providers"])


def test_switch_provider_post_unchanged(lite_app):
    """The POST /{name} switch path is unaffected by the list-route fix."""
    _write_apikey()

    r = lite_app.post("/api/providers/aliyun")
    assert r.status_code == 200, r.text
    assert r.json()["current"] == "aliyun"


def test_switch_unknown_provider_reports_valid_names(lite_app):
    _write_apikey()

    r = lite_app.post("/api/providers/nope")
    assert r.status_code == 200, r.text
    body = r.json()
    assert "error" in body
    assert body["valid"] == ["deepseek", "aliyun"]
