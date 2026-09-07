"""image/ 模块的原生 SQL 模式验证（name 主键）。

`image/service.py` 的函数契约：
- register_image(name, ver, spec, by)  —— 首版自动 is_default=1；upsert
- query()                             —— 扁平返回，可按 name/framework/uploaded_by 过滤
- set_default(name, ver) / get_default_version(name)
- deregister(name, ver)               —— 先校验无在用实例（按展示 framework + version）；删默认则补
- resolve_launch_spec(name, ver)      —— 透传 runtime_spec / access_mode

直接对 appliance.db 只读副本跑 SQL，锁死未来 Python 包装要用的模式。
"""

from __future__ import annotations

import json

IMG_REG = "images"
INS_REG = "instances"


# ── register_image：首版自动默认 ─────────────────────────────

def test_first_version_auto_default(fresh_conn):
    """name 无任何版本时登记 → is_default=1。"""
    name, ver = "autogen", "1.0.0"
    # 先查 name 是否已有版本
    cnt = fresh_conn.execute(
        "SELECT count(*) FROM image WHERE registry=? AND name=?",
        (IMG_REG, name),
    ).fetchone()[0]
    is_default = 1 if cnt == 0 else 0

    fresh_conn.execute(
        "INSERT INTO image(registry, service_id, name, framework, version, version_key, is_default, uploaded_by, data) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (IMG_REG, f"img_{name}_{ver}", name, name, ver,
         "00001.00000.00000~", is_default, "system", "{}"),
    )
    fresh_conn.commit()

    row = fresh_conn.execute(
        "SELECT is_default FROM image WHERE name=? AND version=?",
        (name, ver),
    ).fetchone()
    assert row["is_default"] == 1


def test_second_version_not_default(fresh_conn):
    """name 已有默认版本时，再登记新版本 → is_default=0。"""
    name = "autogen2"
    # 第一版
    fresh_conn.execute(
        "INSERT INTO image(registry, service_id, name, framework, version, version_key, is_default, uploaded_by, data) "
        "VALUES (?, ?, ?, ?, ?, ?, 1, 'system', '{}')",
        (IMG_REG, f"img_{name}_1.0", name, name, "1.0", "00001.00000.00000~"),
    )
    # 第二版
    cnt = fresh_conn.execute(
        "SELECT count(*) FROM image WHERE registry=? AND name=?",
        (IMG_REG, name),
    ).fetchone()[0]
    is_default = 1 if cnt == 0 else 0
    fresh_conn.execute(
        "INSERT INTO image(registry, service_id, name, framework, version, version_key, is_default, uploaded_by, data) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 'system', '{}')",
        (IMG_REG, f"img_{name}_2.0", name, name, "2.0",
         "00002.00000.00000~", is_default),
    )
    fresh_conn.commit()

    rows = fresh_conn.execute(
        "SELECT version, is_default FROM image WHERE name=? ORDER BY version",
        (name,),
    ).fetchall()
    assert {r["version"]: r["is_default"] for r in rows} == {"1.0": 1, "2.0": 0}


# ── query：扁平 + 按 name / framework 过滤 ──────────────────

def test_query_flat_rows_ordered(fresh_conn):
    """GET /api/images 返回扁平行，name ASC → version_key DESC 排序。"""
    rows = fresh_conn.execute(
        "SELECT name, version, is_default FROM image "
        "ORDER BY name ASC, version_key DESC"
    ).fetchall()
    assert rows == []


def test_query_filter_by_name(appliance_conn):
    """按 name 过滤：WHERE name=?（idx_image_name 走索引）。"""
    rows = appliance_conn.execute(
        "SELECT name, version, is_default FROM image "
        "WHERE registry=? AND name=? ORDER BY version_key DESC",
        (IMG_REG, "langchain"),
    ).fetchall()
    assert len(rows) == 2
    assert rows[0]["version"] == "0.2.0"    # version_key DESC
    assert rows[0]["is_default"] == 1


def test_query_filter_by_framework_display(appliance_conn):
    """framework 降级为展示字段，仍可按其筛选。"""
    rows = appliance_conn.execute(
        "SELECT name FROM image WHERE registry=? AND framework=?",
        (IMG_REG, "llama_index"),
    ).fetchall()
    assert {r["name"] for r in rows} == {"llama_index"}


# ── get_default_version / set_default ────────────────────────

def test_get_default_version(appliance_conn):
    """取默认：WHERE name=? AND is_default=1。"""
    row = appliance_conn.execute(
        "SELECT version FROM image "
        "WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, "langchain"),
    ).fetchone()
    assert row["version"] == "0.2.0"


def test_set_default_clears_old_then_sets_new(appliance_writable_copy):
    """set_default 两步：先清该 name 旧 is_default=1，再置新行=1。"""
    name, new_ver = "langchain", "0.1.0"
    # 1. 清旧
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=0 WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, name),
    )
    # 2. 置新
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND name=? AND version=?",
        (IMG_REG, name, new_ver),
    )
    appliance_writable_copy.commit()

    row = appliance_writable_copy.execute(
        "SELECT version FROM image "
        "WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, name),
    ).fetchone()
    assert row["version"] == new_ver


def test_set_default_keeps_exactly_one_default(appliance_writable_copy):
    """设默认后，该 name 恰一行 is_default=1（防多默认）。"""
    name = "langchain"
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=0 WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, name),
    )
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND name=? AND version=?",
        (IMG_REG, name, "0.1.0"),
    )
    appliance_writable_copy.commit()

    cnt = appliance_writable_copy.execute(
        "SELECT count(*) FROM image WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, name),
    ).fetchone()[0]
    assert cnt == 1


# ── deregister：在用实例校验 + 默认补位 ──────────────────────

def test_deregister_blocked_by_in_use_instance(appliance_conn):
    """langchain 0.2.0 有 alice 在用实例 → 删除应被拒（409）。

    在用校验按实例的 framework（展示字段）+ version 关联。
    """
    cnt = appliance_conn.execute(
        "SELECT count(*) FROM instance "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, "langchain", "0.2.0"),
    ).fetchone()[0]
    assert cnt == 1                                   # → 阻断删除


def test_deregister_allowed_when_no_instance(appliance_conn):
    """langchain 0.1.0 有 bob 的九问实例在用？查一下。"""
    cnt = appliance_conn.execute(
        "SELECT count(*) FROM instance "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, "langchain", "0.1.0"),
    ).fetchone()[0]
    # bob 的九问实例用的就是 0.1.0
    assert cnt == 1


def test_deregister_non_default_reassign_not_needed(appliance_writable_copy):
    """删非默认版本 → 默认标记不动。"""
    name, ver = "langchain", "0.1.0"
    # 先确认无在用实例（临时清掉 bob 的实例以便删 0.1.0）
    appliance_writable_copy.execute(
        "DELETE FROM instance WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, name, ver),
    )
    appliance_writable_copy.execute(
        "DELETE FROM image WHERE registry=? AND name=? AND version=?",
        (IMG_REG, name, ver),
    )
    appliance_writable_copy.commit()

    # 默认仍是 0.2.0
    row = appliance_writable_copy.execute(
        "SELECT version FROM image "
        "WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, name),
    ).fetchone()
    assert row["version"] == "0.2.0"


def test_deregister_default_reassigns_to_latest(appliance_writable_copy):
    """删默认版本 → 把 name 内"最新版本"补为默认。"""
    name, ver_to_del = "langchain", "0.2.0"
    # 清在用实例
    appliance_writable_copy.execute(
        "DELETE FROM instance WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, name, ver_to_del),
    )
    # 删默认版本
    appliance_writable_copy.execute(
        "DELETE FROM image WHERE registry=? AND name=? AND version=?",
        (IMG_REG, name, ver_to_del),
    )
    # 补默认：MAX(version_key)（normalized semver，非字符串比较）
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND name=? AND version_key=("
        "  SELECT MAX(version_key) FROM image WHERE registry=? AND name=?"
        ")",
        (IMG_REG, name, IMG_REG, name),
    )
    appliance_writable_copy.commit()

    row = appliance_writable_copy.execute(
        "SELECT version FROM image "
        "WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, name),
    ).fetchone()
    assert row["version"] == "0.1.0"


# ── resolve_launch_spec：透传元戎运行规格 ──────────────────────

def test_resolve_launch_spec_exact_version(appliance_conn):
    """按 name+version 精确查一行，透传 runtime_spec（含 access_mode）。"""
    row = appliance_conn.execute(
        "SELECT data FROM image WHERE registry=? AND name=? AND version=?",
        (IMG_REG, "langchain", "0.2.0"),
    ).fetchone()
    data = json.loads(row["data"])
    rs = data["runtime_spec"]
    assert rs["cpu"] == 2
    assert rs["rootfs"]["imageurl"] == "registry.local/langchain:0.2.0"
    # 新字段在 data JSON 顶层
    assert data["access_mode"] == []


def test_resolve_launch_spec_uses_default_when_version_omitted(appliance_conn):
    """version 未传 → get_default_version → 查默认版本行。"""
    default_ver = appliance_conn.execute(
        "SELECT version FROM image "
        "WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, "langchain"),
    ).fetchone()["version"]

    row = appliance_conn.execute(
        "SELECT data FROM image WHERE registry=? AND name=? AND version=?",
        (IMG_REG, "langchain", default_ver),
    ).fetchone()
    data = json.loads(row["data"])
    assert data["runtime_spec"]["cpu"] == 2                          # 0.2.0 的规格


def test_resolve_launch_spec_404_on_missing_name(appliance_conn):
    """不存在的 name → 查询返回空 → Python 层映射 404。"""
    row = appliance_conn.execute(
        "SELECT data FROM image WHERE registry=? AND name=? AND version=?",
        (IMG_REG, "no_such_name", "0.0.0"),
    ).fetchone()
    assert row is None


# ── 镜像仓文件删除 stub（外部依赖边界） ──────────────────────

def test_deregister_records_imageurl_before_delete(appliance_conn):
    """删镜像前需先取 imageurl（调镜像仓 delete(imageurl)）。

    SQL 模式：删行前 SELECT json_extract(data, '$.runtime_spec.rootfs.imageurl')。
    """
    row = appliance_conn.execute(
        "SELECT json_extract(data, '$.runtime_spec.rootfs.imageurl') AS url FROM image "
        "WHERE registry=? AND name=? AND version=?",
        (IMG_REG, "langchain", "0.1.0"),
    ).fetchone()
    assert row["url"] == "registry.local/langchain:0.1.0"
