"""skill 名路径穿越回归测试。

历史缺陷：SKILL.md frontmatter 的 ``name`` 未经校验就拼成
``<dataset>/skills/<name>``，``name: ..`` 会让 save_skill_zip() 执行
``shutil.rmtree(<dataset>/skills/..)`` —— 一个上传请求即清空整个数据集。
ZIP 条目路径有 zip-slip 防护，frontmatter 这条路径没有。
"""
from __future__ import annotations

import io
import json
import zipfile

import pytest

from a2x_registry.register.store import RegistryStore, validate_skill_name


def _skill_zip(name: str, description: str = "demo") -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("SKILL.md", f"---\nname: {name}\ndescription: {description}\n---\n")
        zf.writestr("main.py", "print('hi')\n")
    return buf.getvalue()


@pytest.fixture
def dataset(tmp_path):
    ds = tmp_path / "demo"
    ds.mkdir()
    (ds / "service.json").write_text(json.dumps({"services": []}), encoding="utf-8")
    (ds / "precious.txt").write_text("important", encoding="utf-8")
    return ds


@pytest.mark.parametrize(
    "bad",
    ["..", ".", "../../escape", "a/b", "a\\b", "a:b", "", None, " pad ", "CON"],
)
def test_validate_skill_name_rejects_unsafe_names(bad):
    with pytest.raises(ValueError):
        validate_skill_name(bad)


@pytest.mark.parametrize("ok", ["my-skill", "skill_1", "技能", "skill.v2"])
def test_validate_skill_name_keeps_legacy_names(ok):
    assert validate_skill_name(ok) == ok


def test_save_skill_zip_rejects_dotdot_and_keeps_dataset(dataset):
    store = RegistryStore(dataset)

    with pytest.raises(ValueError):
        store.save_skill_zip(_skill_zip(".."))

    assert (dataset / "service.json").exists()
    assert (dataset / "precious.txt").exists()
    assert not (dataset / "SKILL.md").exists()


def test_save_skill_zip_rejects_nested_traversal(dataset):
    store = RegistryStore(dataset)

    with pytest.raises(ValueError):
        store.save_skill_zip(_skill_zip("../../escape"))

    assert (dataset / "service.json").exists()


def test_rename_skill_rejects_traversal(dataset):
    store = RegistryStore(dataset)
    assert store.save_skill_zip(_skill_zip("good-skill")).name == "good-skill"

    with pytest.raises(ValueError):
        store.rename_skill("good-skill", "..")

    assert (dataset / "skills" / "good-skill" / "SKILL.md").exists()


def test_remove_skill_rejects_traversal(dataset):
    store = RegistryStore(dataset)

    with pytest.raises(ValueError):
        store.remove_skill("..")

    assert (dataset / "service.json").exists()


def test_valid_skill_upload_still_works(dataset):
    store = RegistryStore(dataset)

    data = store.save_skill_zip(_skill_zip("good-skill"))

    assert data.name == "good-skill"
    assert data.skill_path == "skills/good-skill"
    assert (dataset / "skills" / "good-skill" / "SKILL.md").exists()
