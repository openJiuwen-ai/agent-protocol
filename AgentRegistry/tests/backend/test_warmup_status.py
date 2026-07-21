"""Regression: GET /api/warmup-status must stay JSON-serializable.

Bug (gitcode agent-protocol #35 "/api/warmup-status接口500"): the endpoint
returned the whole ``warmup_state`` dict. That dict doubles as a stash for
background-daemon handles (``_heartbeat_sweeper`` / ``_cluster_anti_entropy``
/ ``_cluster_keepalive``) so shutdown logic can reach them. Those objects
hold ``threading.Lock``s, which ``fastapi.encoders.jsonable_encoder`` cannot
serialize ("'_thread.lock' object is not iterable") → 500, and the frontend
loading screen (which polls this endpoint) never advances.

The fix returns only the public, underscore-free status fields.
"""

from __future__ import annotations

import threading


def test_warmup_status_ok_and_public_fields(lite_app):
    """Real warmup stashes ``_heartbeat_sweeper`` — the endpoint must still 200."""
    r = lite_app.get("/api/warmup-status")
    assert r.status_code == 200, r.text

    body = r.json()
    # Public contract the frontend reads (App.tsx: data.stage/progress/ready).
    assert "ready" in body and "stage" in body and "progress" in body
    # No internal daemon handle leaks into the response.
    assert not any(k.startswith("_") for k in body), body


class _LockHolder:
    """Stand-in for a sweeper: not iterable, no __dict__-friendly encoding."""

    def __init__(self):
        self._lock = threading.Lock()


def test_warmup_status_filters_nonserializable_handles(lite_app):
    """Inject a Lock-bearing handle (as real daemons do) → must not 500."""
    from a2x_registry.backend.startup import warmup_state

    warmup_state["_injected_daemon"] = _LockHolder()
    warmup_state["_raw_lock"] = threading.Lock()
    try:
        r = lite_app.get("/api/warmup-status")
        assert r.status_code == 200, r.text
        body = r.json()
        assert "_injected_daemon" not in body
        assert "_raw_lock" not in body
        # Public fields survive the filter.
        assert "ready" in body
    finally:
        warmup_state.pop("_injected_daemon", None)
        warmup_state.pop("_raw_lock", None)
