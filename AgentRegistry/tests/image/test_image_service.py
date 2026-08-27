"""ImageService 业务逻辑测试（memory 后端，name 主键模型）。

覆盖：
- register_image：首次自动默认、re-register 保留默认 + 更新 runtime_spec、第二版本非默认、
  新字段（description/package_path/image_archive_path/access_mode）落 data、framework 纯展示
- query：扁平返回、name/framework/uploaded_by 过滤、分页、total 计数、runtime_spec 透传、
  name ASC → version_key DESC 排序
- get_default_version：显式默认 + 未设取最新；按 name 维度（同名不同 framework 只一个默认）
- set_default：清旧置新
- resolve_launch_spec：带/不带 version、runtime_spec/access_mode 透传
- deregister：无在用->删、有在用->409、删默认->补最新、镜像不存在->404
"""

from __future__ import annotations

import pytest

from a2x_registry.image.errors import ImageInUseError, ImageNotFoundError
from a2x_registry.image.service import ImageService

from .conftest import make_runtime_spec, make_register_body, make_access_mode


def _reg(svc, name="opencode", ver="v0.2.0", framework="opencode", **kw):
    """Helper: call register_image with body fields."""
    body = make_register_body(name=name, version=ver, framework=framework, **kw)
    return svc.register_image(
        name=name,
        version=ver,
        runtime_spec=body["runtime_spec"],
        uploaded_by=body["uploaded_by"],
        framework=body["framework"],
        description=body["description"],
        package_path=body["package_path"],
        image_archive_path=body["image_archive_path"],
        access_mode=body["access_mode"],
        env_vars=body["env_vars"],
        workspace=body["workspace"],
        mounts=body["mounts"],
        image_module_version=body["image_module_version"],
    )


# ── register_image ──────────────────────────────────────────────

def test_register_first_version_becomes_default(image_svc: ImageService):
    result = _reg(image_svc)
    assert result == {
        "name": "opencode",
        "framework": "opencode",
        "version": "v0.2.0",
        "status": "registered",
    }


def test_register_second_version_not_default(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    result = _reg(image_svc, ver="v0.1.0")
    rows, _ = image_svc.query(name="opencode")
    defaults = [r for r in rows if r["is_default"]]
    assert len(defaults) == 1
    assert defaults[0]["version"] == "v0.2.0"
    assert result["status"] == "registered"


def test_register_reregister_preserves_default_and_updates_runtime_spec(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=1000))
    result = _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=2000))
    assert result["status"] == "updated"
    spec = image_svc.resolve_launch_spec("opencode", "v0.2.0")
    assert spec["runtime_spec"]["cpu"] == 2000


def test_register_empty_name_rejected(image_svc: ImageService):
    with pytest.raises(Exception):
        _reg(image_svc, name="", ver="v0.2.0")


def test_register_new_fields_persisted(image_svc: ImageService):
    """新字段 description/package_path/image_archive_path/access_mode 落 data。"""
    _reg(
        image_svc,
        description="opencode 适配镜像",
        package_path="/pkg/opencode/",
        image_archive_path="/archive/opencode.tar",
    )
    rows, _ = image_svc.query(name="opencode")
    row = rows[0]
    assert row["description"] == "opencode 适配镜像"
    assert row["package_path"] == "/pkg/opencode/"
    assert row["image_archive_path"] == "/archive/opencode.tar"
    assert row["access_mode"] == make_access_mode()


def test_register_framework_is_display_only(image_svc: ImageService):
    """framework 与 name 可不同；定位 / 默认版本均按 name。"""
    _reg(image_svc, name="openclaw", framework="openclaw-fw", ver="v0.2.0")
    _reg(image_svc, name="openclaw", framework="openclaw-fw", ver="v0.1.0")
    rows, _ = image_svc.query(name="openclaw")
    assert len(rows) == 2
    assert all(r["framework"] == "openclaw-fw" for r in rows)
    # 默认版本按 name 维度：恰一个
    defaults = [r for r in rows if r["is_default"]]
    assert len(defaults) == 1
    assert defaults[0]["version"] == "v0.2.0"


def test_register_same_name_different_framework_separate(image_svc: ImageService):
    """name 是主键：不同 name 即使 framework 相同也是两套版本集。"""
    _reg(image_svc, name="openclaw", framework="openclaw", ver="v0.2.0")
    _reg(image_svc, name="openclaw-pro", framework="openclaw", ver="v0.2.0")
    rows, total = image_svc.query()
    assert total == 2
    assert {r["name"] for r in rows} == {"openclaw", "openclaw-pro"}


def test_register_version_key_is_stored(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.10.0")
    rows, _ = image_svc.query(name="opencode")
    versions = [r["version"] for r in rows]
    assert versions == ["v0.10.0", "v0.2.0"]


# ── query (flat + paginated) ────────────────────────────────────

def test_query_flat_returns_rows(image_svc: ImageService):
    _reg(image_svc, name="opencode", ver="v0.2.0")
    _reg(image_svc, name="ninequery", ver="v1.0.0")
    rows, total = image_svc.query()
    assert total == 2
    assert len(rows) == 2
    names = {r["name"] for r in rows}
    assert names == {"opencode", "ninequery"}


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
    # 过渡期：framework_version 是 version 的 deprecated 别名
    assert rows[0]["framework_version"] == rows[0]["version"]
    # no flat imageurl/cpu/memory/ports/env fields
    assert "imageurl" not in rows[0]
    assert "cpu" not in rows[0]
    assert "env" not in rows[0]


def test_query_sorted_name_asc_version_desc(image_svc: ImageService):
    """排序 name ASC → version_key DESC（新版本在前，同 name 相邻）。"""
    _reg(image_svc, name="b-img", ver="v0.1.0")
    _reg(image_svc, name="a-img", ver="v0.2.0")
    _reg(image_svc, name="a-img", ver="v0.10.0")
    _reg(image_svc, name="a-img", ver="v0.1.0")
    rows, _ = image_svc.query()
    got = [(r["name"], r["version"]) for r in rows]
    assert got == [
        ("a-img", "v0.10.0"),
        ("a-img", "v0.2.0"),
        ("a-img", "v0.1.0"),
        ("b-img", "v0.1.0"),
    ]


def test_query_filter_by_name(image_svc: ImageService):
    _reg(image_svc, name="opencode", ver="v0.2.0")
    _reg(image_svc, name="ninequery", ver="v1.0.0")
    rows, total = image_svc.query(name="opencode")
    assert total == 1
    assert rows[0]["name"] == "opencode"


def test_query_filter_by_framework_display_field(image_svc: ImageService):
    """framework 降级为展示字段，但仍可按其筛选。"""
    _reg(image_svc, name="openclaw", framework="openclaw-fw")
    _reg(image_svc, name="ninequery", framework="ninequery-fw", ver="v1.0.0")
    rows, total = image_svc.query(framework="openclaw-fw")
    assert total == 1
    assert rows[0]["name"] == "openclaw"


def test_query_filter_by_uploaded_by(image_svc: ImageService):
    _reg(image_svc, name="opencode", uploaded_by="alice")
    _reg(image_svc, name="ninequery", ver="v1.0.0", uploaded_by="bob")
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


def test_get_default_version_name_not_found(image_svc: ImageService):
    with pytest.raises(ImageNotFoundError):
        image_svc.get_default_version("nonexistent")


# ── set_default ─────────────────────────────────────────────────

def test_set_default_clears_old_and_sets_new(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    _reg(image_svc, ver="v0.1.0")
    result = image_svc.set_default("opencode", "v0.1.0")
    assert result == {
        "name": "opencode",
        "framework": "opencode",
        "default": "v0.1.0",
        "status": "updated",
    }
    assert image_svc.get_default_version("opencode") == "v0.1.0"


def test_set_default_is_per_name(image_svc: ImageService):
    """设默认只影响同 name 的版本（framework 不再参与定位）。"""
    _reg(image_svc, name="openclaw", framework="openclaw-fw", ver="v0.2.0")
    _reg(image_svc, name="openclaw-pro", framework="openclaw-fw", ver="v0.1.0")
    image_svc.set_default("openclaw-pro", "v0.1.0")
    # openclaw 的默认不受影响
    assert image_svc.get_default_version("openclaw") == "v0.2.0"


def test_set_default_target_not_found(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    with pytest.raises(ImageNotFoundError):
        image_svc.set_default("opencode", "v9.9.9")


# ── resolve_launch_spec ─────────────────────────────────────────

def test_resolve_launch_spec_with_version(image_svc: ImageService):
    _reg(image_svc, runtime_spec=make_runtime_spec(cpu=1500))
    spec = image_svc.resolve_launch_spec("opencode", "v0.2.0")
    assert spec["name"] == "opencode"
    assert spec["framework"] == "opencode"
    assert spec["version"] == "v0.2.0"
    assert spec["runtime_spec"]["cpu"] == 1500
    assert spec["runtime_spec"]["rootfs"]["imageurl"] == "harbor.local/adapted/opencode:v0.2.0"
    assert spec["env_vars"] == {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}
    assert spec["workspace"] == "/app"
    assert spec["image_module_version"] == "v1.3"
    assert spec["access_mode"] == make_access_mode()
    assert "cpu" not in spec
    assert "imageurl" not in spec


def test_resolve_launch_spec_uses_default_version(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0", runtime_spec=make_runtime_spec(cpu=1000))
    _reg(image_svc, ver="v0.1.0", runtime_spec=make_runtime_spec(cpu=500))
    spec = image_svc.resolve_launch_spec("opencode")
    assert spec["version"] == "v0.2.0"
    assert spec["runtime_spec"]["cpu"] == 1000


def test_resolve_launch_spec_not_found(image_svc: ImageService):
    with pytest.raises(ImageNotFoundError):
        image_svc.resolve_launch_spec("nonexistent")


# ── deregister ──────────────────────────────────────────────────

def test_deregister_removes_version(image_svc: ImageService):
    _reg(image_svc, ver="v0.2.0")
    result = image_svc.deregister("opencode", "v0.2.0")
    assert result == {
        "name": "opencode",
        "framework": "opencode",
        "version": "v0.2.0",
        "status": "deregistered",
    }
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
    """在用校验：实例按 (framework, framework_version) 关联镜像（展示字段 + version）。"""
    _reg(image_svc, framework="opencode", ver="v0.2.0")
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


def test_deregister_without_framework_cannot_be_in_use(image_svc: ImageService):
    """framework 为空的镜像无法被实例引用（实例契约带 framework），可直接删。"""
    _reg(image_svc, framework=None)
    result = image_svc.deregister("opencode", "v0.2.0")
    assert result["status"] == "deregistered"


def test_deregister_not_found(image_svc: ImageService):
    with pytest.raises(ImageNotFoundError):
        image_svc.deregister("nonexistent", "v0.0.0")
