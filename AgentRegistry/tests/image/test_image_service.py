"""ImageService 业务逻辑测试（memory 后端，runtime_spec 透传模型）。

覆盖：
- register_image：首次自动默认、re-register 保留默认 + 更新 runtime_spec、第二版本非默认、version_key 落库
- query：扁平返回、framework/uploaded_by 过滤、分页、total 计数、runtime_spec 透传
- get_default_version：显式默认 + 未设取最新
- set_default：清旧置新
- resolve_launch_spec：带/不带 version、runtime_spec 透传
- deregister：无在用->删、有在用->409、删默认->补最新、镜像不存在->404
"""

from __future__ import annotations

import pytest

from a2x_registry.image.errors import ImageInUseError, ImageNotFoundError
from a2x_registry.image.service import ImageService

from .conftest import make_runtime_spec, make_register_body


def _default_user() -> str:
    return "user-01"


def _reg(svc, fw="opencode", ver="v0.2.0", **kw):
    """Helper: call register_image with body fields."""
    body = make_register_body(**kw)
    return svc.register_image(
        framework=fw,
        framework_version=ver,
        runtime_spec=body["runtime_spec"],
        env_vars=body["env_vars"],
        workspace=body["workspace"],
        mounts=body["mounts"],
        image_module_version=body["image_module_version"],
        uploaded_by=body["uploaded_by"],
    )


# ── register_image ──────────────────────────────────────────────

def test_register_first_version_becomes_default(image_svc: ImageService):
    result = _reg(image_svc)
    assert result == {
        "framework": "opencode",
        "framework_version": "v0.2.0",
        "is_default": True,
        "status": "registered",
    }


def test_register_second_version_not_default(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    result = _reg(image_svc, ver="v0.1.0")
    assert result["is_default"] is False
    assert result["status"] == "registered"


def test_register_reregister_preserves_default_and_updates_runtime_spec(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=1000))
    result = _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=2000))
    assert result["is_default"] is True
    assert result["status"] == "updated"
    spec = image_svc.resolve_launch_spec("opencode", "v0.2.0")
    assert spec["runtime_spec"]["cpu"] == 2000


def test_register_empty_framework_rejected(image_svc: ImageService):
    with pytest.raises(Exception):
        _reg(image_svc, fw="", ver="v0.2.0")


def test_register_version_key_is_stored(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.10.0")
    rows, total = image_svc.query(framework="opencode")
    versions = [r["framework_version"] for r in rows]
    assert versions == ["v0.10.0", "v0.2.0"]


# ── query (flat + paginated) ────────────────────────────────────

def test_query_flat_returns_rows(image_svc: ImageService):
    _reg(image_svc, fw="opencode", ver="v0.2.0")
    _reg(image_svc, fw="ninequery", ver="v1.0.0")
    rows, total = image_svc.query()
    assert total == 2
    assert len(rows) == 2
    frameworks = {r["framework"] for r in rows}
    assert frameworks == {"opencode", "ninequery"}


def test_query_returns_runtime_spec_passthrough(image_svc: ImageService):
    """runtime_spec is stored and returned as opaque JSON passthrough."""
    _reg(image_svc, runtime_spec=make_runtime_spec(cpu=1500))
    rows, _ = image_svc.query()
    assert rows[0]["runtime_spec"]["cpu"] == 1500
    assert rows[0]["runtime_spec"]["rootfs"]["imageurl"] == "harbor.local/adapted/opencode:v0.2.0"
    assert rows[0]["workspace"] == "/app"
    assert rows[0]["env_vars"] == {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    assert rows[0]["mounts"] == [{"source": "/data/agent", "target": "/data"}]
    assert rows[0]["image_module_version"] == "v1.3"
    # no flat imageurl/cpu/memory/ports/env fields
    assert "imageurl" not in rows[0]
    assert "cpu" not in rows[0]
    assert "env" not in rows[0]


def test_query_filter_by_framework(image_svc: ImageService):
    _reg(image_svc, fw="opencode", ver="v0.2.0")
    _reg(image_svc, fw="ninequery", ver="v1.0.0")
    rows, total = image_svc.query(framework="opencode")
    assert total == 1
    assert rows[0]["framework"] == "opencode"


def test_query_filter_by_uploaded_by(image_svc: ImageService):
    _reg(image_svc, fw="opencode", ver="v0.2.0", uploaded_by="alice")
    _reg(image_svc, fw="ninequery", ver="v1.0.0", uploaded_by="bob")
    rows, total = image_svc.query(uploaded_by="alice")
    assert total == 1
    assert rows[0]["uploaded_by"] == "alice"


def test_query_pagination(image_svc: ImageService):
    for ver in ["v0.3.0", "v0.2.0", "v0.1.0"]:
        _reg(image_svc, ver=ver)
    rows, total = image_svc.query(size=2, page=1)
    assert total == 3
    assert len(rows) == 2


def test_query_pagination_page2(image_svc: ImageService):
    for ver in ["v0.3.0", "v0.2.0", "v0.1.0"]:
        _reg(image_svc, ver=ver)
    rows, total = image_svc.query(size=2, page=2)
    assert total == 3
    assert len(rows) == 1


def test_query_empty_returns_empty(image_svc: ImageService):
    rows, total = image_svc.query()
    assert rows == []
    assert total == 0


# ── get_default_version ─────────────────────────────────────────

def test_get_default_version_explicit(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.1.0")
    assert image_svc.get_default_version("opencode") == "v0.2.0"


def test_get_default_version_falls_back_to_latest(image_svc: ImageService):
    _reg(image_svc, ver="v0.1.0")
    _reg(image_svc, ver="v0.2.0")
    from a2x_registry.common.ids import image_sid
    sid = image_sid("opencode", "v0.1.0")
    image_svc._table_svc.patch("images", sid, {"is_default": 0})
    sid2 = image_sid("opencode", "v0.2.0")
    image_svc._table_svc.patch("images", sid2, {"is_default": 0})
    assert image_svc.get_default_version("opencode") == "v0.2.0"


def test_get_default_version_framework_not_found(image_svc: ImageService):
    with pytest.raises(ImageNotFoundError):
        image_svc.get_default_version("nonexistent")


# ── set_default ─────────────────────────────────────────────────

def test_set_default_clears_old_and_sets_new(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.1.0")
    result = image_svc.set_default("opencode", "v0.1.0")
    assert result == {"framework": "opencode", "default": "v0.1.0", "status": "updated"}
    assert image_svc.get_default_version("opencode") == "v0.1.0"


def test_set_default_target_not_found(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    with pytest.raises(ImageNotFoundError):
        image_svc.set_default("opencode", "v9.9.9")


# ── resolve_launch_spec ─────────────────────────────────────────

def test_resolve_launch_spec_with_version(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=1500))
    spec = image_svc.resolve_launch_spec("opencode", "v0.2.0")
    assert spec["framework"] == "opencode"
    assert spec["framework_version"] == "v0.2.0"
    assert spec["runtime_spec"]["cpu"] == 1500
    assert spec["runtime_spec"]["rootfs"]["imageurl"] == "harbor.local/adapted/opencode:v0.2.0"
    assert spec["env_vars"] == {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    assert spec["workspace"] == "/app"
    assert spec["image_module_version"] == "v1.3"
    assert "cpu" not in spec
    assert "imageurl" not in spec


def test_resolve_launch_spec_uses_default_version(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=1000))
    _reg(image_svc, ver="v0.1.0", runtime_spec=make_runtime_spec(cpu=500))
    spec = image_svc.resolve_launch_spec("opencode")
    assert spec["framework_version"] == "v0.2.0"
    assert spec["runtime_spec"]["cpu"] == 1000


def test_resolve_launch_spec_not_found(image_svc: ImageService):
    with pytest.raises(ImageNotFoundError):
        image_svc.resolve_launch_spec("nonexistent")


# ── deregister ──────────────────────────────────────────────────

def test_deregister_removes_version(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    result = image_svc.deregister("opencode", "v0.2.0")
    assert result["status"] == "deregistered"
    rows, _ = image_svc.query()
    assert rows == []


def test_deregister_default_promotes_latest(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.1.0")
    image_svc.deregister("opencode", "v0.2.0")
    assert image_svc.get_default_version("opencode") == "v0.1.0"


def test_deregister_non_default_keeps_default(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.1.0")
    image_svc.deregister("opencode", "v0.1.0")
    assert image_svc.get_default_version("opencode") == "v0.2.0"


def test_deregister_in_use_raises_409(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    image_svc._table_svc.register("instances", {
        "service_id": "generic_abc123",
        "kind": "三方",
        "framework": "opencode",
        "framework_version": "v0.2.0",
        "node": "node-1",
        "user": "user-01",
        "data": {},
    })
    with pytest.raises(ImageInUseError):
        image_svc.deregister("opencode", "v0.2.0")
    rows, _ = image_svc.query()
    assert len(rows) == 1


def test_deregister_not_found(image_svc: ImageService):
    with pytest.raises(ImageNotFoundError):
        image_svc.deregister("nonexistent", "v0.0.0")


def test_deregister_repo_stub_does_not_block(image_svc: ImageService, monkeypatch):
    monkeypatch.delenv("A2X_REGISTRY_REPO_BASE", raising=False)
    _reg(image_svc, ver="v0.2.0")
    result = image_svc.deregister("opencode", "v0.2.0")
    assert result["repo_deleted"] is False
    assert result["status"] == "deregistered"
