"""Built-in jiuwenswarm image seeding (appliance mode)."""

from __future__ import annotations

from a2x_registry.image.builtin import (
    BUILTIN_IMAGE_FRAMEWORK,
    BUILTIN_IMAGE_NAME,
    BUILTIN_IMAGE_RUNTIME_SPEC,
    seed_builtin_image,
)


def _rows(image_svc):
    rows, _ = image_svc.query(name=BUILTIN_IMAGE_NAME)
    return rows


def test_seed_creates_system_default_row(image_svc):
    assert seed_builtin_image(image_svc, version="1.0.0") == "created"

    rows = _rows(image_svc)
    assert len(rows) == 1
    row = rows[0]
    assert row["name"] == BUILTIN_IMAGE_NAME
    assert row["framework"] == BUILTIN_IMAGE_FRAMEWORK
    assert row["version"] == "1.0.0"
    assert row["is_default"] is True
    assert row["uploaded_by"] == "system"
    assert row["runtime_spec"] == BUILTIN_IMAGE_RUNTIME_SPEC


def test_seed_is_idempotent(image_svc):
    seed_builtin_image(image_svc, version="1.0.0")
    first = _rows(image_svc)[0]

    assert seed_builtin_image(image_svc, version="1.0.0") == "unchanged"

    rows = _rows(image_svc)
    assert len(rows) == 1
    assert rows[0]["created_at"] == first["created_at"]


def test_seed_replaces_old_version(image_svc):
    seed_builtin_image(image_svc, version="1.0.0")

    assert seed_builtin_image(image_svc, version="2.0.0") == "updated"

    rows = _rows(image_svc)
    assert [row["version"] for row in rows] == ["2.0.0"]
    assert rows[0]["is_default"] is True


def test_seed_replaces_in_use_old_version(image_svc, table_svc):
    seed_builtin_image(image_svc, version="1.0.0")
    table_svc.register("instances", {
        "service_id": "generic_seed_test",
        "kind": "九问",
        "framework": BUILTIN_IMAGE_FRAMEWORK,
        "framework_version": "1.0.0",
        "node": "10.0.0.1",
        "user": "alice",
        "data": {"address": "http://10.0.0.1:18092"},
    })

    assert seed_builtin_image(image_svc, version="2.0.0") == "updated"

    assert [row["version"] for row in _rows(image_svc)] == ["2.0.0"]
    assert len(table_svc.query("instances", {"user": "alice"})) == 1
