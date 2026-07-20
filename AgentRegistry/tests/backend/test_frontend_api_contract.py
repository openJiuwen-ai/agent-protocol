"""Guard the frontend↔backend API contract against silent drift.

The clone-only web UI (``ui/frontend``) talks to the backend purely over
HTTP. When the backend evolves but the UI lags (or vice-versa), a route
can quietly disappear from under a ``fetch`` call — exactly how
"ui界面调用不存在的接口" (GET /api/providers 404) slipped in.

These tests pin the routes the frontend actually calls. They assert the
route *exists with the right verb* (a missing route returns Starlette's
default ``{"detail": "Not Found"}``; a handler that raises 404 — e.g.
"taxonomy not built yet" — still proves the route is wired). They do not
assert full behavior; per-feature suites cover that.

If you add/rename a backend route the UI depends on, update this list.
"""

from __future__ import annotations

import json
import uuid

from a2x_registry.common.paths import llm_apikey_path


def _write_apikey() -> None:
    """Minimal llm_apikey.json so the providers route doesn't 503 on config."""
    path = llm_apikey_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({"providers": [
            {"name": "deepseek", "base_url": "x", "model": "deepseek-chat",
             "api_keys": ["sk-test"]},
        ]}),
        encoding="utf-8",
    )


def _assert_route_wired(resp, method: str, path: str) -> None:
    """Fail only when the route is genuinely absent or the verb is wrong.

    A handler-raised 404 (custom detail) means the route IS wired — the
    resource just doesn't exist yet, which the frontend handles.
    """
    route_missing = (
        resp.status_code == 404
        and resp.headers.get("content-type", "").startswith("application/json")
        and resp.json() == {"detail": "Not Found"}
    )
    assert not route_missing, f"ROUTE MISSING: {method} {path}"
    assert resp.status_code != 405, f"WRONG VERB: {method} {path} -> 405"


# Endpoints the UI hits before any dataset is selected (page load).
def test_page_load_endpoints_resolve(lite_app):
    _write_apikey()

    # GET /api/providers MUST resolve at the no-slash path without a 307
    # (Bug: ui界面调用不存在的接口). follow_redirects=False makes a stray
    # redirect a hard failure.
    r = lite_app.get("/api/providers", follow_redirects=False)
    assert r.status_code == 200, f"GET /api/providers -> {r.status_code}"

    for method, path in [
        ("GET", "/api/warmup-status"),
        ("GET", "/api/datasets"),
        ("GET", "/api/datasets/embedding-models"),
    ]:
        r = lite_app.request(method, path, follow_redirects=False)
        _assert_route_wired(r, method, path)
        assert r.status_code == 200, f"{method} {path} -> {r.status_code}"


# Endpoints the UI hits once a dataset is selected — probed on a fresh
# (empty) dataset, so a handler 404 ("not built yet") is acceptable; only
# a missing route / wrong verb fails.
def test_per_dataset_endpoints_are_wired(lite_app):
    ds = "ds_" + uuid.uuid4().hex[:8]
    assert lite_app.post("/api/datasets", json={"name": ds}).status_code == 200
    try:
        for method, path in [
            ("GET", f"/api/datasets/{ds}/taxonomy"),
            ("GET", f"/api/datasets/{ds}/default-queries"),
            ("GET", f"/api/datasets/{ds}/services?fields=brief"),
            ("GET", f"/api/datasets/{ds}/vector-config"),
            ("GET", f"/api/datasets/{ds}/build/status"),
            # build/stream is SSE — probing it would block on the event
            # stream; its existence is covered by the static route map.
        ]:
            r = lite_app.request(method, path, follow_redirects=False)
            _assert_route_wired(r, method, path)
    finally:
        lite_app.delete(f"/api/datasets/{ds}")
