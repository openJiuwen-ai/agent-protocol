"""Query (build) feature tests.

The taxonomy-build endpoint is part of the query feature because A2X
search depends on a built taxonomy. Build behavior:

- POST /api/datasets/{ds}/build         — available on lite: build is a
                                          pure-LLM workflow; only needs
                                          ``llm_apikey.json``. The actual
                                          build runs in a background task;
                                          the trigger returns 200 immediately.
- GET  /api/datasets/{ds}/build/status  — light: works in both modes
"""

from __future__ import annotations


def test_build_trigger_works_in_lite(lite_app, dataset):
    """Build trigger returns 200 in lite mode — the route was un-gated when
    A2X build became pure-LLM. The async background task may still fail if
    ``llm_apikey.json`` is missing, but that's out of band from this response.
    """
    r = lite_app.post(
        f"/api/datasets/{dataset}/build", json={"resume": "no"}
    )
    assert r.status_code == 200
    body = r.json()
    assert body["dataset"] == dataset
    assert body["status"] == "started"


def test_build_status_works_in_lite(lite_app, dataset):
    """Build status reads ``_build_jobs`` dict; no extras needed."""
    r = lite_app.get(f"/api/datasets/{dataset}/build/status")
    assert r.status_code == 200
    assert r.json()["status"] == "idle"


def test_build_status_route_full(full_app):
    full_app.post("/api/datasets", json={"name": "full_status"})
    r = full_app.get("/api/datasets/full_status/build/status")
    assert r.status_code == 200
    assert r.json()["status"] == "idle"
