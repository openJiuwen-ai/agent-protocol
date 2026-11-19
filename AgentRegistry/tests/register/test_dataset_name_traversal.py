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


# ---------------------------------------------------------------------------
# 六个入口方法：读路径同样要先校验再落盘
# ---------------------------------------------------------------------------

_ENTRY_POINTS = [
    ("_ensure_dataset_initialized", lambda svc, name: svc._ensure_dataset_initialized(name)),
    ("_compute_taxonomy_state", lambda svc, name: svc._compute_taxonomy_state(name)),
    ("is_auth_required", lambda svc, name: svc.is_auth_required(name)),
    ("set_auth_config", lambda svc, name: svc.set_auth_config(name, True)),
    ("get_lease_config", lambda svc, name: svc.get_lease_config(name)),
    ("set_lease_config", lambda svc, name: svc.set_lease_config(
        name, enabled=True, min_ttl=10, max_ttl=60, grace_period=30)),
]


@pytest.mark.parametrize(
    "bad", ["..", "../escape", ".", "a/b", "a\\b", "bad\x00name"]
)
@pytest.mark.parametrize(
    "label, call", _ENTRY_POINTS, ids=[entry[0] for entry in _ENTRY_POINTS]
)
def test_entry_points_validate_name_before_touching_disk(home, label, call, bad):
    """这六处曾直接拼 ``database_dir / dataset`` 做 ``.exists()``：

    调用方用 ``..`` 之类的名字就能让存在性探测（以及
    ``_compute_taxonomy_state`` 的读文件）落在注册表根目录之外。
    """
    svc = RegistryService(home / "database")
    outside = home / "outside"
    outside.mkdir()

    with pytest.raises(ValueError):
        call(svc, bad)

    assert list(outside.iterdir()) == []
    assert home.exists()
    assert (home / "auth_data.json").exists()
    assert (home / "database" / "demo" / "service.json").exists()


def test_existing_dataset_dir_distinguishes_bad_name_from_missing(home):
    """路由层靠异常类型分流：非法名 → 400，合法但不存在 → 404。"""
    from a2x_registry.register.errors import RegistryNotFoundError

    svc = RegistryService(home / "database")

    with pytest.raises(ValueError):
        svc.existing_dataset_dir("../escape")
    with pytest.raises(RegistryNotFoundError):
        svc.existing_dataset_dir("no_such_dataset")

    assert svc.existing_dataset_dir("demo") == home / "database" / "demo"


# ---------------------------------------------------------------------------
# 长度上限是「创建入口」策略：历史超长数据集仍必须可读、可删
# ---------------------------------------------------------------------------


def test_delete_dataset_ignores_length_cap(home):
    """删除路径不套用长度上限：超长名会一路走到「不存在」，而不是被长度拒。"""
    from a2x_registry.register.service import _MAX_DATASET_NAME_LEN

    svc = RegistryService(home / "database")
    long_name = "d" * (_MAX_DATASET_NAME_LEN + 20)

    with pytest.raises(ValueError, match="does not exist"):
        svc.delete_dataset(long_name)

    # 创建入口的长度上限不变。
    with pytest.raises(ValueError, match="too long"):
        svc.create_dataset("n" * (_MAX_DATASET_NAME_LEN + 1))


def test_legacy_over_long_dataset_still_deletable(home):
    from a2x_registry.register.service import _MAX_DATASET_NAME_LEN

    long_name = "d" * (_MAX_DATASET_NAME_LEN + 20)
    legacy = home / "database" / long_name
    try:
        legacy.mkdir()
    except OSError as exc:  # Windows MAX_PATH（未开启长路径）下建不出来
        pytest.skip(f"filesystem cannot hold a {len(long_name)}-char dir: {exc}")
    (legacy / "register_config.json").write_text("{}", encoding="utf-8")

    svc = RegistryService(home / "database")

    assert long_name in svc.list_datasets()
    assert svc.dataset_exists(long_name) is True

    svc.delete_dataset(long_name)
    assert not legacy.exists()
    assert (home / "database" / "demo" / "service.json").exists()

