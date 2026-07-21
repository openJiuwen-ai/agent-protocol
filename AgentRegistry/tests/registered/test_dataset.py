"""Registered (dataset creation) feature tests.

Dataset creation is a prerequisite for registering anything. The lite
default install must be able to create a dataset, with the default
embedding model wired into the response and the A2A format version
exposed for SDK clients.
"""

from __future__ import annotations

import uuid


def test_create_dataset(lite_app):
    name = "ds_" + uuid.uuid4().hex[:8]
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    try:
        body = r.json()
        assert body["dataset"] == name
        assert body["embedding_model"] == "all-MiniLM-L6-v2"
        assert body["formats"]["a2a"] == "v0.0"
    finally:
        r = lite_app.delete(f"/api/datasets/{name}")
        assert r.status_code == 200, r.text
