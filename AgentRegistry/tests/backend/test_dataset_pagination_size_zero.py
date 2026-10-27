# coding: utf-8
"""GET /api/datasets/{dataset}/services?size=0 must be a client error.

``size`` is a page size; the handler divides by it when it computes page
counts, so ``size=0`` used to fail with a server error instead of a 4xx.
``size`` must be ``-1`` (all) or a positive page size, and these tests pin
that contract through the same ``lite_app`` client the other backend tests
use.
"""


def test_size_zero_is_rejected_as_a_request_error(lite_app):
    assert lite_app.post("/api/datasets", json={"name": "demo"}).status_code == 200

    response = lite_app.get("/api/datasets/demo/services", params={"size": 0})

    assert response.status_code == 400, response.text
    assert "size" in response.json()["detail"]


def test_size_minus_one_still_returns_every_service(lite_app):
    assert lite_app.post("/api/datasets", json={"name": "demo"}).status_code == 200

    response = lite_app.get("/api/datasets/demo/services", params={"size": -1})

    assert response.status_code == 200, response.text
    assert response.json() == []
