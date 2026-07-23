"""image/ 模块的原生 SQL 模式验证。

`image/service.py` 的函数契约：
- register_image(fw, ver, spec, by)  —— 首版自动 is_default=1；upsert
- query()                            —— 按 framework 分组层次化
- set_default(fw, ver) / get_default_version(fw)
- deregister(fw, ver)                —— 先校验无在用实例；删默认则补
- resolve_launch_spec(fw, ver)       —— 抽 rootfs/cpu/memory/ports/env

直接对 appliance.db 只读副本跑 SQL，锁死未来 Python 包装要用的模式。
"""

from __future__ import annotations

import json

IMG_REG = "images"
INS_REG = "instances"


# ── register_image：首版自动默认 ─────────────────────────────

def test_first_version_auto_default(fresh_conn):
    """框架无任何版本时登记 → is_default=1。"""
    fw, ver = "autogen", "1.0.0"
    # 先查框架是否已有版本
    cnt = fresh_conn.execute(
        "SELECT count(*) FROM image WHERE registry=? AND framework=?",
        (IMG_REG, fw),
    ).fetchone()[0]
    is_default = 1 if cnt == 0 else 0

    fresh_conn.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, data) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (IMG_REG, f"img_{fw}_{ver}", fw, ver, "00001.00000.00000~", is_default, "{}"),
    )
    fresh_conn.commit()

    row = fresh_conn.execute(
        "SELECT is_default FROM image WHERE framework=? AND framework_version=?",
        (fw, ver),
    ).fetchone()
    assert row["is_default"] == 1


def test_second_version_not_default(fresh_conn):
    """框架已有默认版本时，再登记新版本 → is_default=0。"""
    fw = "autogen2"
    # 第一版
    fresh_conn.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, data) "
        "VALUES (?, ?, ?, ?, ?, 1, '{}')",
        (IMG_REG, f"img_{fw}_1.0", fw, "1.0", "00001.00000.00000~"),
    )
    # 第二版
    cnt = fresh_conn.execute(
        "SELECT count(*) FROM image WHERE registry=? AND framework=?",
        (IMG_REG, fw),
    ).fetchone()[0]
    is_default = 1 if cnt == 0 else 0
    fresh_conn.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, data) "
        "VALUES (?, ?, ?, ?, ?, ?, '{}')",
        (IMG_REG, f"img_{fw}_2.0", fw, "2.0", "00002.00000.00000~", is_default),
    )
    fresh_conn.commit()

    rows = fresh_conn.execute(
        "SELECT framework_version, is_default FROM image WHERE framework=? ORDER BY framework_version",
        (fw,),
    ).fetchall()
    assert {r["framework_version"]: r["is_default"] for r in rows} == {"1.0": 1, "2.0": 0}


# ── query：按 framework 分组层次化 ───────────────────────────

def test_query_grouped_by_framework(appliance_conn):
    """GET /api/images 返回 {framework, default, versions:[...]} 层次结构。

    SQL 模式：先取所有版本行，Python 侧按 framework 聚合（或用 GROUP BY）。
    """
    rows = appliance_conn.execute(
        "SELECT framework, framework_version, is_default FROM image "
        "ORDER BY framework, framework_version"
    ).fetchall()

    grouped = {}
    for r in rows:
        grouped.setdefault(r["framework"], []).append(
            {"version": r["framework_version"], "is_default": bool(r["is_default"])}
        )

    assert set(grouped) == {"langchain", "llama_index"}
    assert len(grouped["langchain"]) == 2
    assert grouped["llama_index"][0]["is_default"] is True


# ── get_default_version / set_default ────────────────────────

def test_get_default_version(appliance_conn):
    """取默认：WHERE framework=? AND is_default=1。"""
    row = appliance_conn.execute(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, "langchain"),
    ).fetchone()
    assert row["framework_version"] == "0.2.0"


def test_set_default_clears_old_then_sets_new(appliance_writable_copy):
    """set_default 两步：先清该框架旧 is_default=1，再置新行=1。"""
    fw, new_ver = "langchain", "0.1.0"
    # 1. 清旧
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=0 WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    # 2. 置新
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, new_ver),
    )
    appliance_writable_copy.commit()

    row = appliance_writable_copy.execute(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    ).fetchone()
    assert row["framework_version"] == new_ver


def test_set_default_keeps_exactly_one_default(appliance_writable_copy):
    """设默认后，该框架恰一行 is_default=1（防多默认）。"""
    fw = "langchain"
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=0 WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, "0.1.0"),
    )
    appliance_writable_copy.commit()

    cnt = appliance_writable_copy.execute(
        "SELECT count(*) FROM image WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    ).fetchone()[0]
    assert cnt == 1


# ── deregister：在用实例校验 + 默认补位 ──────────────────────

def test_deregister_blocked_by_in_use_instance(appliance_conn):
    """langchain 0.2.0 有 alice 在用实例 → 删除应被拒（409）。"""
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
    fw, ver = "langchain", "0.1.0"
    # 先确认无在用实例（临时清掉 bob 的实例以便删 0.1.0）
    appliance_writable_copy.execute(
        "DELETE FROM instance WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, fw, ver),
    )
    appliance_writable_copy.execute(
        "DELETE FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, ver),
    )
    appliance_writable_copy.commit()

    # 默认仍是 0.2.0
    row = appliance_writable_copy.execute(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    ).fetchone()
    assert row["framework_version"] == "0.2.0"


def test_deregister_default_reassigns_to_latest(appliance_writable_copy):
    """删默认版本 → 把框架内"最新版本"补为默认。
    """
    fw, ver_to_del = "langchain", "0.2.0"
    # 清在用实例
    appliance_writable_copy.execute(
        "DELETE FROM instance WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, fw, ver_to_del),
    )
    # 删默认版本
    appliance_writable_copy.execute(
        "DELETE FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, ver_to_del),
    )
    # 补默认：MAX(framework_version)
    appliance_writable_copy.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND framework=? AND framework_version=("
        "  SELECT MAX(framework_version) FROM image WHERE registry=? AND framework=?"
        ")",
        (IMG_REG, fw, IMG_REG, fw),
    )
    appliance_writable_copy.commit()

    row = appliance_writable_copy.execute(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    ).fetchone()
    assert row["framework_version"] == "0.1.0"


# ── resolve_launch_spec：抽元戎运行规格 ──────────────────────

def test_resolve_launch_spec_exact_version(appliance_conn):
    """按 framework+version 精确查一行，抽 runtime_spec 中的 cpu/imageurl。"""
    row = appliance_conn.execute(
        "SELECT data FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "langchain", "0.2.0"),
    ).fetchone()
    data = json.loads(row["data"])
    rs = data["runtime_spec"]
    assert rs["cpu"] == 2
    assert rs["rootfs"]["imageurl"] == "registry.local/langchain:0.2.0"


def test_resolve_launch_spec_uses_default_when_version_omitted(appliance_conn):
    """version 未传 → get_default_version → 查默认版本行。"""
    default_ver = appliance_conn.execute(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, "langchain"),
    ).fetchone()["framework_version"]

    row = appliance_conn.execute(
        "SELECT data FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "langchain", default_ver),
    ).fetchone()
    data = json.loads(row["data"])
    assert data["runtime_spec"]["cpu"] == 2                          # 0.2.0 的规格


def test_resolve_launch_spec_404_on_missing_framework(appliance_conn):
    """不存在的 framework → 查询返回空 → Python 层映射 404。"""
    row = appliance_conn.execute(
        "SELECT data FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "no_such_fw", "0.0.0"),
    ).fetchone()
    assert row is None


# ── 镜像仓文件删除 stub（外部依赖边界） ──────────────────────

def test_deregister_records_imageurl_before_delete(appliance_conn):
    """删镜像前需先取 imageurl（调镜像仓 delete(imageurl)）。

    SQL 模式：删行前 SELECT json_extract(data, '$.runtime_spec.rootfs.imageurl')。
    """
    row = appliance_conn.execute(
        "SELECT json_extract(data, '$.runtime_spec.rootfs.imageurl') AS url FROM image "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "langchain", "0.1.0"),
    ).fetchone()
    assert row["url"] == "registry.local/langchain:0.1.0"
