"""Dataset 名路径穿越回归测试。

历史缺陷：``delete_dataset("..")`` 会 ``shutil.rmtree(<database_dir>/..)``，
一次调用删掉整个注册表 home 目录（auth_data.json / cluster_state.json /
全部数据集）；``create_dataset("../x")`` 同理可在数据库根目录之外建目录。
"""
from __future__ import annotations

import json

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
    ["..", ".", "../escape", "a/b", "a\\b", "a:b", "", None, " padded ", "NUL"],
)
def test_validate_dataset_name_rejects_unsafe_names(bad):
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


def test_normal_dataset_still_deletable(home):
    svc = RegistryService(home / "database")

    assert svc.dataset_dir("demo") == home / "database" / "demo"
    svc.delete_dataset("demo")
    assert not (home / "database" / "demo").exists()
