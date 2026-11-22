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
import threading
import zipfile

import pytest

from a2x_registry.common.auth_context import AuthContext
from a2x_registry.register.service import RegistryService
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


# ---------------------------------------------------------------------------
# ZIP 条目名：必须在原始条目名（ZipInfo.orig_filename）上校验
# ---------------------------------------------------------------------------


def _zip_with_raw_entry(raw_name: str) -> bytes:
    """构造条目名未经规范化的 ZIP（含合法 SKILL.md）。

    ``zipfile`` 在 **写** 的时候就会动手脚：``ZipInfo.__init__`` 把名字在第一个
    NUL 处截断、并把 ``os.sep`` 换成 ``/``（Windows 上就是反斜杠）。所以先按正常
    名字构造 ``ZipInfo``，再把 ``filename`` 覆盖成原始字节串，才能造出真实攻击者
    能打出来的条目名。
    """
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("SKILL.md", "---\nname: good-skill\ndescription: demo\n---\n")
        info = zipfile.ZipInfo("placeholder.py")
        info.filename = raw_name
        zf.writestr(info, "x")
    return buf.getvalue()


def test_zip_nul_is_rejected_via_original_entry_name(dataset):
    """``ZipInfo.filename`` 在第一个 NUL 处被截断，NUL 只有 orig_filename 里还在。

    在 ``filename`` 上查 NUL 永远不命中 —— 这正是原始意见指出的漏口。
    """
    store = RegistryStore(dataset)

    payload = _zip_with_raw_entry("evil\x00x.py")

    with pytest.raises(ValueError, match="NUL"):
        store.save_skill_zip(payload)

    assert (dataset / "service.json").exists()
    assert not (dataset / "skills" / "good-skill").exists()


@pytest.mark.parametrize(
    "entry, reason",
    [
        ("C:/evil.py", "drive prefix"),
        ("/etc/evil.py", "absolute path"),
        ("a/../../evil.py", "parent reference"),
    ],
)
def test_zip_path_validation_fires_with_valid_manifest(dataset, entry, reason):
    """ZIP 里带合法 SKILL.md 时，报错必须来自路径校验，而不是「缺少清单」。"""
    store = RegistryStore(dataset)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("SKILL.md", "---\nname: good-skill\ndescription: demo\n---\n")
        zf.writestr(entry, "x")

    with pytest.raises(ValueError, match=reason):
        store.save_skill_zip(buf.getvalue())

    assert not (dataset / "skills" / "good-skill").exists()


@pytest.mark.parametrize(
    "entry",
    ["sub\\evil.py", "\\\\server\\share\\evil.py"],
)
def test_zip_backslash_entry_rejected_with_valid_manifest(dataset, entry):
    """反斜杠条目在 Windows 解压时是分隔符（``\\\\srv\\share`` 还是 UNC 绝对路径），

    必须显式拒绝；这类名字只能用原始条目名构造，见 ``_zip_with_raw_entry``。
    """
    store = RegistryStore(dataset)

    with pytest.raises(ValueError, match="backslash"):
        store.save_skill_zip(_zip_with_raw_entry(entry))

    assert not (dataset / "skills" / "good-skill").exists()


@pytest.mark.skipif(os.name != "nt", reason="大小写不敏感是 Windows 文件系统的特性")
def test_load_skills_accepts_case_only_difference_on_windows(dataset):
    """``skills/My-Skill`` + frontmatter ``name: my-skill`` 在 Windows 上就是同一个目录。"""
    folder = dataset / "skills" / "My-Skill"
    folder.mkdir(parents=True)
    (folder / "SKILL.md").write_text(
        "---\nname: my-skill\ndescription: x\n---\n", encoding="utf-8"
    )

    entries = RegistryStore(dataset).load_skills()

    assert [e.skill_data.name for e in entries] == ["my-skill"]


# ---------------------------------------------------------------------------
# service 层：属主检查前置、注销失败不回滚内存、上传与注销互斥
# ---------------------------------------------------------------------------


@pytest.fixture
def service(tmp_path):
    db = tmp_path / "database"
    ds = db / "demo"
    ds.mkdir(parents=True)
    (ds / "service.json").write_text(json.dumps({"services": []}), encoding="utf-8")
    return RegistryService(db), ds


def test_owner_check_runs_before_the_zip_is_extracted(service):
    """非属主上传必须在落盘前被 403 拦下：否则原目录已被 rmtree，无法恢复。"""
    svc, ds = service
    owner = AuthContext(principal_id="owner-1", role="provider", namespaces=frozenset({"demo"}))
    intruder = AuthContext(principal_id="intruder", role="provider", namespaces=frozenset({"demo"}))

    svc.register_skill("demo", _skill_zip("alpha", description="owner"), owner)
    skill_md = ds / "skills" / "alpha" / "SKILL.md"
    before = skill_md.read_text(encoding="utf-8")

    with pytest.raises(PermissionError):
        svc.register_skill("demo", _skill_zip("alpha", description="intruder"), intruder)

    assert skill_md.read_text(encoding="utf-8") == before
    assert (ds / "precious.txt").exists() is False  # 数据集里本来就没有这个文件
    assert [e.skill_data.name for e in svc.list_entries("demo")] == ["alpha"]


def test_deregister_failure_leaves_entry_and_folder_untouched(service, monkeypatch):
    """删盘失败（如名字校验抛 ValueError）时内存条目与 service.json 不能变。"""
    svc, ds = service
    svc.register_skill("demo", _skill_zip("alpha"))
    service_json = ds / "service.json"
    before = service_json.read_text(encoding="utf-8")

    def boom(name):
        raise ValueError(f"invalid skill name: {name!r}")

    monkeypatch.setattr(svc._get_store("demo"), "remove_skill", boom)

    with pytest.raises(ValueError):
        svc.deregister_skill("demo", "alpha")

    assert [e.skill_data.name for e in svc.list_entries("demo")] == ["alpha"]
    assert (ds / "skills" / "alpha" / "SKILL.md").exists()
    assert service_json.read_text(encoding="utf-8") == before


def test_owner_can_replace_own_skill(service):
    """属主同名重传的主路径：名字预读 → 属主通过 → 覆盖落盘 → 条目 updated。"""
    svc, ds = service
    owner = AuthContext(principal_id="owner-1", role="provider", namespaces=frozenset({"demo"}))

    first = svc.register_skill("demo", _skill_zip("alpha", description="v1"), owner)
    skill_md = ds / "skills" / "alpha" / "SKILL.md"
    assert "v1" in skill_md.read_text(encoding="utf-8")

    second = svc.register_skill("demo", _skill_zip("alpha", description="v2"), owner)

    assert second.status == "updated"
    assert second.service_id == first.service_id
    on_disk = skill_md.read_text(encoding="utf-8")
    assert "v2" in on_disk
    assert "v1" not in on_disk
    entries = svc.list_entries("demo")
    assert [(e.service_id, e.skill_data.description) for e in entries] == [
        (first.service_id, "v2")
    ]


def test_failed_extraction_keeps_previous_skill(dataset):
    """解压中途失败（条目名冲突）不能销毁原 skill，也不能留下暂存目录。"""
    store = RegistryStore(dataset)
    store.save_skill_zip(_skill_zip("alpha", description="v1"))
    skill_md = dataset / "skills" / "alpha" / "SKILL.md"
    before = skill_md.read_text(encoding="utf-8")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("SKILL.md", "---\nname: alpha\ndescription: v2\n---\n")
        # 合法条目名，但解压时 ``conflict`` 先是文件、再被当成目录父级 ——
        # 校验阶段拦不住，只有写到一半才会失败。
        zf.writestr("conflict", "file")
        zf.writestr("conflict/inner.py", "x")

    with pytest.raises(OSError):
        store.save_skill_zip(buf.getvalue())

    assert skill_md.read_text(encoding="utf-8") == before
    leftovers = [p.name for p in (dataset / "skills").iterdir() if p.name.startswith(".")]
    assert leftovers == []


def test_upload_and_deregister_share_one_lock(service, monkeypatch):
    """注销删盘与重传必须互斥：否则注销会把重传刚写好的条目一起 pop 掉。"""
    svc, ds = service
    svc.register_skill("demo", _skill_zip("alpha"))

    store = svc._get_store("demo")
    real_remove = store.remove_skill
    inside = threading.Event()
    release = threading.Event()

    def blocking_remove(name):
        inside.set()
        assert release.wait(10), "deregister was never released"
        return real_remove(name)

    monkeypatch.setattr(store, "remove_skill", blocking_remove)

    results = {}
    down = threading.Thread(
        target=lambda: results.setdefault("down", svc.deregister_skill("demo", "alpha"))
    )
    down.start()
    assert inside.wait(10), "deregister never reached remove_skill"

    up = threading.Thread(
        target=lambda: results.setdefault("up", svc.register_skill("demo", _skill_zip("alpha")))
    )
    up.start()
    up.join(0.3)
    assert up.is_alive(), "upload must wait for the deregister holding the skill lock"

    release.set()
    down.join(10)
    up.join(10)
    assert not down.is_alive() and not up.is_alive()

    assert results["down"].status == "deleted"
    assert results["up"].status == "registered"
    assert [e.skill_data.name for e in svc.list_entries("demo")] == ["alpha"]
    assert (ds / "skills" / "alpha" / "SKILL.md").exists()

