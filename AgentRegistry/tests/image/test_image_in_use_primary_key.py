"""镜像在用校验的主键关联（image_name 桥接）测试（TDD：先于实现编写）。

背景：在用校验的关联键曾用可变展示字段 ``framework + version``，与镜像主键
``name`` 脱节，产生误伤（多 name 共用 framework 值）与漏判（PATCH 改
framework 后旧值实例不再匹配）两类边界。实例契约引入 ``image_name``
（镜像主键引用）后，在用校验优先按 ``image_name + version`` 主键关联：

- 实例行带 ``image_name`` → 按主键关联（展示字段改值 / 共用值均不影响）
- 实例行无 ``image_name``（模式 A 存量条目）→ 退回 framework 匹配（保守）
- 实例行的 ``image_name`` 指向**其它**镜像 → 不阻塞本镜像（关联正确）
"""

from __future__ import annotations

import pytest

from a2x_registry.register.errors import ImageInUseError

from .conftest import make_register_body


def _make_instance(
    table_svc,
    service_id: str,
    *,
    framework: str,
    framework_version: str,
    image_name: str = "",
    user: str = "alice",
    instance_id: str = "",
) -> None:
    """直接落一条实例行（绕过 InstanceService，专注在用校验语义）。"""
    table_svc.register("instances", {
        "service_id": service_id,
        "kind": "三方",
        "framework": framework,
        "framework_version": framework_version,
        "node": "192.168.0.12",
        "user": user,
        "data": {
            "address": "10.244.1.7:4096",
            "instance_id": instance_id,
            "image_name": image_name,
            "status": "运行",
        },
    })


# ── 主键关联（image_name 命中）──────────────────────────────────────

def test_in_use_by_image_name(image_svc, table_svc):
    """实例带 image_name + 同版本 → 注销被拦（即使 framework 展示值不同）。"""
    image_svc.register_image(**make_register_body(name="oc-pro", framework="pro-fw"))
    _make_instance(
        table_svc, "user-10+oc-pro",
        framework="oc-pro", framework_version="v0.2.0", image_name="oc-pro",
    )
    with pytest.raises(ImageInUseError):
        image_svc.deregister("oc-pro", "v0.2.0")


def test_in_use_image_name_version_mismatch_allows(image_svc, table_svc):
    """image_name 相同但版本不同 → 不阻塞（按 name+version 精确关联）。"""
    image_svc.register_image(**make_register_body(name="opencode", version="v0.2.0"))
    image_svc.register_image(**make_register_body(name="opencode", version="v0.3.0"))
    _make_instance(
        table_svc, "user-01+opencode",
        framework="opencode", framework_version="v0.2.0", image_name="opencode",
    )
    # v0.3.0 无引用 → 可注销；v0.2.0 被拦
    image_svc.deregister("opencode", "v0.3.0")
    with pytest.raises(ImageInUseError):
        image_svc.deregister("opencode", "v0.2.0")


def test_instance_referencing_other_image_does_not_block(image_svc, table_svc):
    """实例 image_name 指向其它镜像（framework 值恰好相同）→ 不阻塞本镜像。"""
    image_svc.register_image(**make_register_body(name="openclaw", framework="shared-fw"))
    image_svc.register_image(**make_register_body(name="openclaw-pro", framework="shared-fw"))
    _make_instance(
        table_svc, "user-01+openclaw-pro",
        framework="shared-fw", framework_version="v0.2.0",
        image_name="openclaw-pro",
    )
    # 实际由 openclaw-pro 拉起 → openclaw 不被误伤
    image_svc.deregister("openclaw", "v0.2.0")
    with pytest.raises(ImageInUseError):
        image_svc.deregister("openclaw-pro", "v0.2.0")


# ── 存量兼容（无 image_name 退回 framework 匹配）────────────────────

def test_legacy_instance_framework_match_blocks(image_svc, table_svc):
    """无 image_name 的存量行 → 按展示字段 framework 匹配（原行为）。"""
    image_svc.register_image(**make_register_body(name="leg", framework="legacy-abc"))
    _make_instance(
        table_svc, "leg-user+legacy-abc",
        framework="legacy-abc", framework_version="v0.2.0",
    )
    with pytest.raises(ImageInUseError):
        image_svc.deregister("leg", "v0.2.0")


def test_legacy_instance_framework_empty_falls_back_to_name(
    image_svc, table_svc
):
    """镜像 framework 为空 → 按镜像 name 兜底匹配（保守，不漏判）。"""
    image_svc.register_image(**make_register_body(name="bare", framework=None))
    _make_instance(
        table_svc, "u+bare",
        framework="bare", framework_version="v0.2.0",
    )
    with pytest.raises(ImageInUseError):
        image_svc.deregister("bare", "v0.2.0")


# ── 误伤边界关闭（主键关联后 framework 共用值不再互相阻塞）──────────

def test_framework_rename_no_longer_loses_reference(image_svc, table_svc):
    """带 image_name 的在用实例：镜像 framework 改名后在用校验仍命中。"""
    image_svc.register_image(**make_register_body(name="claw", framework="claw-fw"))
    _make_instance(
        table_svc, "u+claw",
        framework="claw-fw", framework_version="v0.2.0", image_name="claw",
    )
    # framework 改名（PATCH 后落库的效果，直接经 update_image 模拟）
    image_svc.update_image("claw", "v0.2.0", {"framework": "claw-renamed"})
    with pytest.raises(ImageInUseError):
        image_svc.deregister("claw", "v0.2.0")


def test_no_in_use_allows_deregister(image_svc, table_svc):
    """无任何引用 → 正常注销。"""
    image_svc.register_image(**make_register_body(name="free", framework="free-fw"))
    _make_instance(
        table_svc, "u+other",
        framework="other-fw", framework_version="v0.9.0", image_name="other",
    )
    image_svc.deregister("free", "v0.2.0")
