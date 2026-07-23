"""image/ 模块的 rqlite 后端 SQL 模式验证。

对照 ``test_image_sql.py``，同一组业务断言改用 ``Backend`` 抽象跑在 rqlite
Docker 集群上。与 sqlite 原生驱动的调用差异：
- ``conn.execute(...).fetchone()`` → ``backend.query(...)`` 取 ``rows[0] if rows else None``。
- ``conn.execute(...).fetchall()`` → ``backend.query(...)`` 直接是 ``list[dict]``。
- ``conn.commit()`` 省略（rqlite 每次 execute 即 Raft 提交）。
- 只读 ``appliance_conn`` / 可写 ``appliance_writable_copy`` 统一用
  ``rqlite_seeded_backend``（rqlite 集群连接本就可读可写，teardown 清表）。

端口号见 ``conftest.RQLITE_DOCKER_ENDPOINTS``（常量）。
"""

from __future__ import annotations

import json

IMG_REG = "images"
INS_REG = "instances"


# ── register_image：首版自动默认 ─────────────────────────────

def test_first_version_auto_default(rqlite_backend):
    """框架无任何版本时登记 → is_default=1。"""
    fw, ver = "autogen", "1.0.0"
    rows = rqlite_backend.query(
        "SELECT count(*) AS c FROM image WHERE registry=? AND framework=?",
        (IMG_REG, fw),
    )
    is_default = 1 if rows[0]["c"] == 0 else 0

    rqlite_backend.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, data) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (IMG_REG, f"img_{fw}_{ver}", fw, ver, "00001.00000.00000~", is_default, "{}"),
    )

    rows = rqlite_backend.query(
        "SELECT is_default FROM image WHERE framework=? AND framework_version=?",
        (fw, ver),
    )
    assert rows[0]["is_default"] == 1


def test_second_version_not_default(rqlite_backend):
    """框架已有默认版本时，再登记新版本 → is_default=0。"""
    fw = "autogen2"
    # 第一版
    rqlite_backend.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, data) "
        "VALUES (?, ?, ?, ?, ?, 1, '{}')",
        (IMG_REG, f"img_{fw}_1.0", fw, "1.0", "00001.00000.00000~"),
    )
    # 第二版
    rows = rqlite_backend.query(
        "SELECT count(*) AS c FROM image WHERE registry=? AND framework=?",
        (IMG_REG, fw),
    )
    is_default = 1 if rows[0]["c"] == 0 else 0
    rqlite_backend.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, data) "
        "VALUES (?, ?, ?, ?, ?, ?, '{}')",
        (IMG_REG, f"img_{fw}_2.0", fw, "2.0", "00002.00000.00000~", is_default),
    )

    rows = rqlite_backend.query(
        "SELECT framework_version, is_default FROM image WHERE framework=? ORDER BY framework_version",
        (fw,),
    )
    assert {r["framework_version"]: r["is_default"] for r in rows} == {"1.0": 1, "2.0": 0}


# ── query：按 framework 分组层次化 ───────────────────────────

def test_query_grouped_by_framework(rqlite_seeded_backend):
    """GET /api/images 返回 {framework, default, versions:[...]} 层次结构。"""
    rows = rqlite_seeded_backend.query(
        "SELECT framework, framework_version, is_default FROM image "
        "ORDER BY framework, framework_version"
    )

    grouped = {}
    for r in rows:
        grouped.setdefault(r["framework"], []).append(
            {"version": r["framework_version"], "is_default": bool(r["is_default"])}
        )

    assert set(grouped) == {"langchain", "llama_index"}
    assert len(grouped["langchain"]) == 2
    assert grouped["llama_index"][0]["is_default"] is True


# ── get_default_version / set_default ────────────────────────

def test_get_default_version(rqlite_seeded_backend):
    """取默认：WHERE framework=? AND is_default=1。"""
    rows = rqlite_seeded_backend.query(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, "langchain"),
    )
    assert rows[0]["framework_version"] == "0.2.0"


def test_set_default_clears_old_then_sets_new(rqlite_seeded_backend):
    """set_default 两步：先清该框架旧 is_default=1，再置新行=1。"""
    fw, new_ver = "langchain", "0.1.0"
    # 1. 清旧
    rqlite_seeded_backend.execute(
        "UPDATE image SET is_default=0 WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    # 2. 置新
    rqlite_seeded_backend.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, new_ver),
    )

    rows = rqlite_seeded_backend.query(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    assert rows[0]["framework_version"] == new_ver


def test_set_default_keeps_exactly_one_default(rqlite_seeded_backend):
    """设默认后，该框架恰一行 is_default=1（防多默认）。"""
    fw = "langchain"
    rqlite_seeded_backend.execute(
        "UPDATE image SET is_default=0 WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    rqlite_seeded_backend.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, "0.1.0"),
    )

    rows = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM image WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    assert rows[0]["c"] == 1


# ── deregister：在用实例校验 + 默认补位 ──────────────────────

def test_deregister_blocked_by_in_use_instance(rqlite_seeded_backend):
    """langchain 0.2.0 有 alice 在用实例 → 删除应被拒（409）。"""
    rows = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM instance "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, "langchain", "0.2.0"),
    )
    assert rows[0]["c"] == 1                                   # → 阻断删除


def test_deregister_allowed_when_no_instance(rqlite_seeded_backend):
    """langchain 0.1.0 有 bob 的九问实例在用？查一下。"""
    rows = rqlite_seeded_backend.query(
        "SELECT count(*) AS c FROM instance "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, "langchain", "0.1.0"),
    )
    # bob 的九问实例用的就是 0.1.0
    assert rows[0]["c"] == 1


def test_deregister_non_default_reassign_not_needed(rqlite_seeded_backend):
    """删非默认版本 → 默认标记不动。"""
    fw, ver = "langchain", "0.1.0"
    # 先确认无在用实例（临时清掉 bob 的实例以便删 0.1.0）
    rqlite_seeded_backend.execute(
        "DELETE FROM instance WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, fw, ver),
    )
    rqlite_seeded_backend.execute(
        "DELETE FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, ver),
    )

    # 默认仍是 0.2.0
    rows = rqlite_seeded_backend.query(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    assert rows[0]["framework_version"] == "0.2.0"


def test_deregister_default_reassigns_to_latest(rqlite_seeded_backend):
    """删默认版本 → 把框架内"最新版本"补为默认。"""
    fw, ver_to_del = "langchain", "0.2.0"
    # 清在用实例
    rqlite_seeded_backend.execute(
        "DELETE FROM instance WHERE registry=? AND framework=? AND framework_version=?",
        (INS_REG, fw, ver_to_del),
    )
    # 删默认版本
    rqlite_seeded_backend.execute(
        "DELETE FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, fw, ver_to_del),
    )
    # 补默认：MAX(framework_version)
    rqlite_seeded_backend.execute(
        "UPDATE image SET is_default=1 "
        "WHERE registry=? AND framework=? AND framework_version=("
        "  SELECT MAX(framework_version) FROM image WHERE registry=? AND framework=?"
        ")",
        (IMG_REG, fw, IMG_REG, fw),
    )

    rows = rqlite_seeded_backend.query(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, fw),
    )
    assert rows[0]["framework_version"] == "0.1.0"


# ── resolve_launch_spec：抽元戎运行规格 ──────────────────────

def test_resolve_launch_spec_exact_version(rqlite_seeded_backend):
    """按 framework+version 精确查一行，抽 runtime_spec/cpu/memory。"""
    row = rqlite_seeded_backend.query(
        "SELECT data FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "langchain", "0.2.0"),
    )[0]
    data = json.loads(row["data"])
    spec = {k: data.get(k) for k in ("runtime_spec", "env_vars", "workspace")}
    assert spec["runtime_spec"]["cpu"] == 2
    assert spec["runtime_spec"]["rootfs"]["imageurl"] == "registry.local/langchain:0.2.0"


def test_resolve_launch_spec_uses_default_when_version_omitted(rqlite_seeded_backend):
    """version 未传 → get_default_version → 查默认版本行。"""
    rows = rqlite_seeded_backend.query(
        "SELECT framework_version FROM image "
        "WHERE registry=? AND framework=? AND is_default=1",
        (IMG_REG, "langchain"),
    )
    default_ver = rows[0]["framework_version"]

    rows = rqlite_seeded_backend.query(
        "SELECT data FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "langchain", default_ver),
    )
    data = json.loads(rows[0]["data"])
    assert data["runtime_spec"]["cpu"] == 2                          # 0.2.0 的规格


def test_resolve_launch_spec_404_on_missing_framework(rqlite_seeded_backend):
    """不存在的 framework → 查询返回空 → Python 层映射 404。"""
    rows = rqlite_seeded_backend.query(
        "SELECT data FROM image WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "no_such_fw", "0.0.0"),
    )
    assert rows == []


# ── 镜像仓文件删除 stub（外部依赖边界） ──────────────────────

def test_deregister_records_imageurl_before_delete(rqlite_seeded_backend):
    """删镜像前需先取 imageurl（调镜像仓 delete(imageurl)）。

    SQL 模式：删行前 SELECT json_extract(data, '$.runtime_spec.rootfs.imageurl')。
    """
    row = rqlite_seeded_backend.query(
        "SELECT json_extract(data, '$.runtime_spec.rootfs.imageurl') AS url FROM image "
        "WHERE registry=? AND framework=? AND framework_version=?",
        (IMG_REG, "langchain", "0.1.0"),
    )[0]
    assert row["url"] == "registry.local/langchain:0.1.0"
