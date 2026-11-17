"""Dataset 名路径穿越回归测试。

历史缺陷：``delete_dataset("..")`` 会 ``shutil.rmtree(<database_dir>/..)``，
一次调用删掉整个注册表 home 目录（auth_data.json / cluster_state.json /
全部数据集）；``create_dataset("../x")`` 同理可在数据库根目录之外建目录。

覆盖两层防护：
* ``validate_dataset_name`` —— 调用方输入的严格策略；
* ``dataset_dir`` 的 resolved 包含检查 + ``dataset_exists`` 的非抛错返回 ——
  读/发现路径既不越界，也不会被磁盘上的历史目录名拖垮。
"""
from __future__ import annotations

import json
import os

import pytest

from a2x_registry.register.service import RegistryService, validate_dataset_name


@pytest.fixture
def home(tmp_path):
    db = tmp_path / "database"
    ds = db / "demo"
    ds.mkdir(parents=True)
    (ds / "service.json").write_text(json.dumps({"services": []}), encoding="utf-8")
    (tmp_path / "auth_data.json").write_text("{}", encoding="utf-8")
    return tmp_path


@pytest.mark.parametrize(
    "bad",
    ["..", ".", "../escape", "a/b", "a\\b", "", None, "line\nbreak", "nul\x00byte"],
)
def test_validate_dataset_name_rejects_unsafe_names(bad):
    with pytest.raises(ValueError):
        validate_dataset_name(bad)


@pytest.mark.skipif(os.name != "nt", reason="这些名字只在 Windows 上别名到其它目录")
@pytest.mark.parametrize(
    "bad",
    ["a:b", " padded ", "NUL", "con", "aux.txt", "demo.", "demo ", "com1"],
)
def test_validate_dataset_name_rejects_windows_alias_names(bad):
    with pytest.raises(ValueError):
        validate_dataset_name(bad)


@pytest.mark.parametrize("ok", ["demo", "my_dataset", "ds-1", "ds.v2", "数据集"])
def test_validate_dataset_name_keeps_legacy_names(ok):
    assert validate_dataset_name(ok) == ok


def test_delete_dataset_rejects_dotdot_and_keeps_home(home):
    svc = RegistryService(home / "database")

    with pytest.raises(ValueError):
        svc.delete_dataset("..")

    assert home.exists()
    assert (home / "auth_data.json").exists()
    assert (home / "database" / "demo" / "service.json").exists()


def test_create_dataset_rejects_traversal(home):
    svc = RegistryService(home / "database")

    with pytest.raises(ValueError):
        svc.create_dataset("../escaped")

    assert not (home / "escaped").exists()


def test_dataset_dir_rejects_traversal(home):
    svc = RegistryService(home / "database")

    with pytest.raises(ValueError):
        svc.dataset_dir("..")


def test_dataset_exists_never_walks_outside_root(home):
    """``dataset_exists`` 是读路径：越界名返回 False，且不抛错。"""
    svc = RegistryService(home / "database")

    for name in ("..", ".", "../escape", "a/b", "a\\b", "", None):
        assert svc.dataset_exists(name) is False

    assert svc.dataset_exists("demo") is True
    assert home.exists()
    assert (home / "auth_data.json").exists()


# ---------------------------------------------------------------------------
# resolved 包含检查：单段合法名字也可能是指向外部的符号链接
# ---------------------------------------------------------------------------


def _make_symlink(link, target):
    try:
        link.symlink_to(target, target_is_directory=True)
    except (OSError, NotImplementedError) as exc:  # Windows 无权限 / 平台不支持
        pytest.skip(f"symlink unavailable: {exc}")


def test_dataset_dir_rejects_symlink_escape(home):
    outside = home / "outside"
    outside.mkdir()
    (outside / "secret.json").write_text("{}", encoding="utf-8")
    _make_symlink(home / "database" / "linked", outside)

    svc = RegistryService(home / "database")

    with pytest.raises(ValueError):
        svc.dataset_dir("linked")

    assert svc.dataset_exists("linked") is False


def test_discovery_skips_unsafe_directories_without_raising(home):
    """磁盘上的越界目录（符号链接）只被跳过，不能让发现/列表面崩掉。"""
    outside = home / "outside"
    outside.mkdir()
    (outside / "service.json").write_text(json.dumps({"services": []}), encoding="utf-8")
    _make_symlink(home / "database" / "linked", outside)

    svc = RegistryService(home / "database")

    assert svc.list_datasets() == ["demo"]
    assert svc._discover_datasets() == []
    assert all(item["name"] != "linked" for item in svc.list_datasets_with_counts())


@pytest.mark.skipif(os.name == "nt", reason="Windows 上这些名字无法创建或会被别名")
def test_legacy_posix_names_still_load_and_delete(home):
    """POSIX 上历史版本创建的 ``:`` / 首尾空白目录名仍可读、可删。"""
    db = home / "database"
    legacy = db / "ns:skill"
    legacy.mkdir()
    (legacy / "register_config.json").write_text("{}", encoding="utf-8")

    svc = RegistryService(home / "database")

    assert "ns:skill" in svc.list_datasets()
    assert svc.dataset_exists("ns:skill") is True
    assert svc.dataset_dir("ns:skill") == legacy

    svc.delete_dataset("ns:skill")
    assert not legacy.exists()
    assert (db / "demo" / "service.json").exists()


def test_normal_dataset_still_deletable(home):
    svc = RegistryService(home / "database")

    assert svc.dataset_dir("demo") == home / "database" / "demo"
    svc.delete_dataset("demo")
    assert not (home / "database" / "demo").exists()
