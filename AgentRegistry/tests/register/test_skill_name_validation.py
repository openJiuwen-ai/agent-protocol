"""skill 名路径穿越回归测试。

历史缺陷：SKILL.md frontmatter 的 ``name`` 未经校验就拼成
``<dataset>/skills/<name>``，``name: ..`` 会让 save_skill_zip() 执行
``shutil.rmtree(<dataset>/skills/..)`` —— 一个上传请求即清空整个数据集。
ZIP 条目路径的旧守卫只拦 ``/`` 开头与 ``..`` 子串，frontmatter 与 Windows
盘符绝对路径都没有覆盖。
"""
from __future__ import annotations

import io
import json
import os
import zipfile

import pytest

from a2x_registry.register.store import RegistryStore, validate_skill_name


def _skill_zip(name: str, description: str = "demo") -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("SKILL.md", f"---\nname: {name}\ndescription: {description}\n---\n")
        zf.writestr("main.py", "print('hi')\n")
    return buf.getvalue()


def _raw_zip(entries) -> bytes:
    """Build a ZIP with arbitrary entry names (no SKILL.md frontmatter)."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for entry in entries:
            zf.writestr(entry, "---\nname: evil\ndescription: x\n---\n")
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
    ["..", ".", "../../escape", "a/b", "a\\b", "", None, "line\nbreak", "nul\x00byte"],
)
def test_validate_skill_name_rejects_unsafe_names(bad):
    with pytest.raises(ValueError):
        validate_skill_name(bad)


@pytest.mark.skipif(os.name != "nt", reason="这些名字只在 Windows 上别名到其它目录")
@pytest.mark.parametrize("bad", ["a:b", " pad ", "CON", "aux.txt", "demo.", "com1"])
def test_validate_skill_name_rejects_windows_alias_names(bad):
    with pytest.raises(ValueError):
        validate_skill_name(bad)


@pytest.mark.parametrize("ok", ["my-skill", "skill_1", "技能", "skill.v2"])
def test_validate_skill_name_keeps_legacy_names(ok):
    assert validate_skill_name(ok) == ok


def test_validate_skill_name_limits_bytes_not_chars():
    """200 个中文 = 600 字节，超过单段文件系统上限，必须按字节拒绝。"""
    assert validate_skill_name("技" * 66) == "技" * 66  # 198 bytes
    with pytest.raises(ValueError):
        validate_skill_name("技" * 67)  # 201 bytes


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


@pytest.mark.parametrize(
    "entry",
    [
        "C:/evil.md",
        "C:evil.md",
        "c:\\evil.md",
        "\\\\server\\share\\evil.md",
        "/etc/evil.md",
        "sub\\..\\..\\evil.md",
        "a/b/../../../evil.md",
        "evil\x00.md",
    ],
)
def test_save_skill_zip_rejects_unsafe_entries(dataset, entry):
    """Windows 盘符 / 反斜杠 / NUL / 绝对路径 / 父引用都要在解压前拒绝。"""
    store = RegistryStore(dataset)

    with pytest.raises(ValueError):
        store.save_skill_zip(_raw_zip([entry]))

    assert (dataset / "service.json").exists()
    assert not (dataset / "skills" / "evil.md").exists()
    assert not (dataset.parent / "evil.md").exists()


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


def test_update_skill_md_rejects_traversal(dataset):
    store = RegistryStore(dataset)

    with pytest.raises(ValueError):
        store.update_skill_md("..", {"description": "x"})

    assert (dataset / "service.json").exists()
    assert not (dataset / "SKILL.md").exists()


def test_safe_rmtree_refuses_outside_skills(dataset):
    """兜底删除保护：数据集目录与 ``skills/`` 根本身都不允许被 rmtree。"""
    store = RegistryStore(dataset)
    store._skills_root().mkdir()

    with pytest.raises(ValueError):
        store._safe_rmtree(dataset)
    with pytest.raises(ValueError):
        store._safe_rmtree(store._skills_root())

    assert (dataset / "service.json").exists()
    assert store._skills_root().is_dir()


def test_get_skill_zip_rejects_traversal(dataset):
    """读侧穿越：``..`` 曾把整个数据集（service.json / auth_config.json）打包返回。"""
    store = RegistryStore(dataset)
    assert store.save_skill_zip(_skill_zip("good-skill")).name == "good-skill"

    with pytest.raises(ValueError):
        store.get_skill_zip("..")

    payload = store.get_skill_zip("good-skill")
    with zipfile.ZipFile(io.BytesIO(payload)) as zf:
        assert "SKILL.md" in zf.namelist()
        assert "service.json" not in zf.namelist()


def test_load_skills_skips_name_that_escapes_folder(dataset):
    """手工放入的 ``name: ..`` 不再被注册（否则 single 查询会把整库打包）。"""
    evil = dataset / "skills" / "evil"
    evil.mkdir(parents=True)
    (evil / "SKILL.md").write_text("---\nname: ..\ndescription: x\n---\n", encoding="utf-8")

    store = RegistryStore(dataset)

    assert store.load_skills() == []


def test_load_skills_skips_name_mismatching_folder(dataset):
    """frontmatter 名与目录名分叉会让 skill_path 指向别的目录，按损坏数据跳过。"""
    folder = dataset / "skills" / "on-disk"
    folder.mkdir(parents=True)
    (folder / "SKILL.md").write_text("---\nname: other\ndescription: x\n---\n", encoding="utf-8")

    store = RegistryStore(dataset)

    assert [e.skill_data.name for e in store.load_skills()] == []
    assert folder.exists()


def test_valid_skill_upload_still_works(dataset):
    store = RegistryStore(dataset)

    data = store.save_skill_zip(_skill_zip("good-skill"))

    assert data.name == "good-skill"
    assert data.skill_path == "skills/good-skill"
    assert (dataset / "skills" / "good-skill" / "SKILL.md").exists()
