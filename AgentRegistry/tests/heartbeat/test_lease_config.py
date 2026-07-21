"""GET / POST /lease-config — public read, admin-or-anon write."""

from __future__ import annotations


def test_default_disabled(lite_app, dataset):
    """A fresh dataset has no lease_config.json → reports enabled=false."""
    r = lite_app.get(f"/api/datasets/{dataset}/lease-config")
    assert r.status_code == 200
    body = r.json()
    assert body["dataset"] == dataset
    assert body["enabled"] is False


def test_enable_persists(lite_app, dataset):
    r = lite_app.post(
        f"/api/datasets/{dataset}/lease-config",
        json={"enabled": True, "min_ttl": 10, "max_ttl": 600, "grace_period": 60},
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["enabled"] is True
    assert body["min_ttl"] == 10
    assert body["max_ttl"] == 600
    assert body["grace_period"] == 60

    # Read-back confirms persistence
    r = lite_app.get(f"/api/datasets/{dataset}/lease-config")
    assert r.json()["enabled"] is True


def test_toggle_off(lite_app, dataset):
    lite_app.post(
        f"/api/datasets/{dataset}/lease-config",
        json={"enabled": True, "min_ttl": 5, "max_ttl": 60, "grace_period": 10},
    )
    r = lite_app.post(
        f"/api/datasets/{dataset}/lease-config",
        json={"enabled": False, "min_ttl": 5, "max_ttl": 60, "grace_period": 10},
    )
    assert r.status_code == 200
    assert r.json()["enabled"] is False


def test_bad_bounds_rejected(lite_app, dataset):
    """min_ttl < 1 or max_ttl < min_ttl → 400."""
    r = lite_app.post(
        f"/api/datasets/{dataset}/lease-config",
        json={"enabled": True, "min_ttl": 0, "max_ttl": 60, "grace_period": 10},
    )
    assert r.status_code == 400

    r = lite_app.post(
        f"/api/datasets/{dataset}/lease-config",
        json={"enabled": True, "min_ttl": 100, "max_ttl": 10, "grace_period": 10},
    )
    assert r.status_code == 400


def test_lease_config_endpoint_404_for_missing_dataset(lite_app):
    r = lite_app.get("/api/datasets/no_such_ds/lease-config")
    assert r.status_code == 404
