from __future__ import annotations

import json
import sqlite3
from typing import Iterator, List, Optional, Sequence

import pytest

from a2x_registry.common.db import Backend, init_schema
from a2x_registry.common.ids import image_sid
from a2x_registry.image.service import ImageService
from a2x_registry.image.version_key import version_key
from a2x_registry.register.service import RegistryTableService

IMG_REG = "images"
TEMP_TABLE = "image_migrated"

# 旧版库的 image 表结构：framework 主定位、framework_version 版本列，
# 无 name / version 列；行主键 (registry, service_id)。
LEGACY_IMAGE_DDL = """
CREATE TABLE registry_meta (
  registry TEXT PRIMARY KEY,
  kind     TEXT NOT NULL,
  config   TEXT
);

CREATE TABLE image (
  registry          TEXT NOT NULL,
  service_id        TEXT NOT NULL,
  framework         TEXT NOT NULL,
  framework_version TEXT NOT NULL,
  version_key       TEXT NOT NULL,
  is_default        INTEGER NOT NULL DEFAULT 0,
  uploaded_by       TEXT,
  data              TEXT NOT NULL,
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX idx_image_fw     ON image(registry, framework);
CREATE INDEX idx_image_fw_ver ON image(registry, framework, framework_version);
"""

# 迁移后 image 表的契约列集（name 主键 / version 更名 / framework 降级）。
_NEW_IMAGE_COLS = {
    "registry", "service_id", "name", "framework", "version",
    "version_key", "is_default", "uploaded_by", "data",
}

# 旧 fw 索引退役，name 索引接管（与 tests/db/test_schema.py 的索引契约一致）。
_NEW_IMAGE_INDEXES = {
    "idx_image_name", "idx_image_name_ver", "idx_image_by", "idx_image_order",
}


# ── fixtures / helpers ─────────────────────────────────────────

@pytest.fixture
def legacy_conn() -> Iterator[sqlite3.Connection]:
    """旧版本库：framework 主键的 image 表已建好（空）、registry_meta 就位。

    模拟一个 之前版本写出的 sqlite 文件被新版本启动加载的初始状态。
    """
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    conn.executescript(LEGACY_IMAGE_DDL)
    conn.execute(
        "INSERT INTO registry_meta(registry, kind) VALUES (?, 'image')",
        (IMG_REG,),
    )
    conn.commit()
    try:
        yield conn
    finally:
        conn.close()


def _seed_legacy_image(
    conn: sqlite3.Connection,
    framework: str,
    version: str,
    *,
    is_default: int = 0,
    uploaded_by: str = "system",
    data: Optional[dict] = None,
    service_id: Optional[str] = None,
) -> None:
    """向旧结构 image 表插入一行（service_id 默认 = image_sid(fw, ver)）。"""
    if data is None:
        data = {
            "runtime_spec": {
                "rootfs": {
                    "imageurl": f"harbor.local/adapted/{framework}:{version}"
                },
                "cpu": 1000,
                "memory": 2048,
            },
            "env_vars": {},
            "workspace": "/app",
            "mounts": [],
            "image_module_version": "v1.0",
            "created_at": "2026-06-01T00:00:00Z",
        }
    conn.execute(
        "INSERT INTO image(registry, service_id, framework, framework_version,"
        " version_key, is_default, uploaded_by, data)"
        " VALUES (?,?,?,?,?,?,?,?)",
        (
            IMG_REG,
            service_id or image_sid(framework, version),
            framework,
            version,
            version_key(version),
            is_default,
            uploaded_by,
            json.dumps(data, ensure_ascii=False),
        ),
    )
    conn.commit()


def _create_temp_image_table(
    conn: sqlite3.Connection, rows: Optional[Sequence[tuple]] = None
) -> None:
    """模拟上次迁移中断后残留的临时表（结构 = 迁移目标的新结构）。

    rows 为已拷入临时表的行（元组顺序 = 9 列契约顺序）。
    """
    conn.execute(
        f"CREATE TABLE {TEMP_TABLE} ("
        " registry TEXT NOT NULL, service_id TEXT NOT NULL,"
        " name TEXT NOT NULL, framework TEXT, version TEXT NOT NULL,"
        " version_key TEXT NOT NULL, is_default INTEGER NOT NULL DEFAULT 0,"
        " uploaded_by TEXT, data TEXT NOT NULL,"
        " PRIMARY KEY (registry, service_id))"
    )
    for r in rows or []:
        conn.execute(
            f"INSERT INTO {TEMP_TABLE} VALUES (?,?,?,?,?,?,?,?,?)", r
        )
    conn.commit()


def _image_rows(conn: sqlite3.Connection) -> List[dict]:
    """读当前 image 表全行（稳定排序，供快照对比）。"""
    return [
        dict(r) for r in conn.execute(
            "SELECT registry, service_id, name, framework, version,"
            " version_key, is_default, uploaded_by, data"
            " FROM image ORDER BY service_id"
        )
    ]


def _columns(conn: sqlite3.Connection, table: str) -> List[str]:
    return [r[1] for r in conn.execute(f"PRAGMA table_info({table})")]


def _tables(conn: sqlite3.Connection) -> set:
    return {
        r[0] for r in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"
        )
    }


def _image_indexes(conn: sqlite3.Connection) -> set:
    return {
        r[0] for r in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='index'"
            " AND tbl_name='image'"
        )
    }


# ── 一、正常迁移：数据不变量（I1/I2/I4） ──────────────────────

def test_migrate_single_row_field_mapping(legacy_conn):
    """单行迁移：回填 name/framework、version 更名，其余字段逐项原样保留。"""
    _seed_legacy_image(
        legacy_conn, "opencode", "v0.2.0",
        is_default=1, uploaded_by="user-01",
    )
    init_schema(legacy_conn)

    rows = _image_rows(legacy_conn)
    assert len(rows) == 1
    r = rows[0]
    assert r["name"] == "opencode"        # I2 回填
    assert r["version"] == "v0.2.0"       # I2 更名
    assert r["framework"] == "opencode"   # I1 framework 保留（降级展示列）
    assert r["service_id"] == image_sid("opencode", "v0.2.0")  # I3
    assert r["is_default"] == 1           # I1
    assert r["uploaded_by"] == "user-01"  # I1
    assert r["version_key"] == version_key("v0.2.0")           # I1
    data = json.loads(r["data"])
    assert data["created_at"] == "2026-06-01T00:00:00Z"        # I1 created_at 不丢
    assert (
        data["runtime_spec"]["rootfs"]["imageurl"]
        == "harbor.local/adapted/opencode:v0.2.0"
    )


def test_migrate_all_rows_and_per_name_defaults(legacy_conn):
    """多 name 多 version：行数不变，旧 framework 维度默认 -> 新 name 维度
    仍每个 name 恰一行默认；name 含特殊字符也逐字回填。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    _seed_legacy_image(legacy_conn, "opencode", "v0.1.0")
    _seed_legacy_image(legacy_conn, "open-claw.v2", "v1.0.0", is_default=1)
    _seed_legacy_image(legacy_conn, "llama_index", "0.9.0", is_default=1)

    init_schema(legacy_conn)

    rows = _image_rows(legacy_conn)
    assert len(rows) == 4                                   # I1 行数不变
    got = {(r["name"], r["version"]): r for r in rows}
    assert set(got) == {                                    # I2 逐行映射
        ("opencode", "v0.2.0"),
        ("opencode", "v0.1.0"),
        ("open-claw.v2", "v1.0.0"),
        ("llama_index", "0.9.0"),
    }
    # I4：每个 name 恰一行默认，且默认版本与旧 framework 维度一致
    for name in ("opencode", "open-claw.v2", "llama_index"):
        defaults = [r for r in rows if r["name"] == name and r["is_default"] == 1]
        assert len(defaults) == 1
    assert got[("opencode", "v0.2.0")]["is_default"] == 1
    assert got[("opencode", "v0.1.0")]["is_default"] == 0


def test_migrate_empty_legacy_table(legacy_conn):
    """空旧表：迁移后仍 0 行，结构照常切换到 name 主键。"""
    init_schema(legacy_conn)
    assert _image_rows(legacy_conn) == []
    assert "name" in _columns(legacy_conn, "image")


def test_migrate_preserves_registry_meta(legacy_conn):
    """registry_meta 既有登记（含镜像 / 实例注册表声明）迁移后原样保留。"""
    legacy_conn.execute(
        "INSERT INTO registry_meta(registry, kind) VALUES ('instances','instance')"
    )
    legacy_conn.commit()

    init_schema(legacy_conn)

    meta = {
        r["registry"]: r["kind"]
        for r in legacy_conn.execute("SELECT registry, kind FROM registry_meta")
    }
    assert meta == {"images": "image", "instances": "instance"}


def test_migrate_legacy_data_json_without_new_fields(legacy_conn):
    """旧 data JSON 不含新字段：迁移既不注入、也不删改任何键。"""
    old_data = {
        "runtime_spec": {"rootfs": {"imageurl": "x:v1"}},
        "created_at": "2026-05-01T00:00:00Z",
    }
    _seed_legacy_image(legacy_conn, "opencode", "v1.0.0", data=old_data)

    init_schema(legacy_conn)

    r = _image_rows(legacy_conn)[0]
    assert json.loads(r["data"]) == old_data               # 逐键原样
    # 新字段读取为 NULL —— 迁移不伪造默认值
    desc = legacy_conn.execute(
        "SELECT json_extract(data,'$.description') FROM image WHERE name='opencode'"
    ).fetchone()[0]
    assert desc is None


def test_migrate_does_not_invent_or_repair_defaults(legacy_conn):
    """迁移是纯搬运不是修复：旧库本就无默认（全 0）时，迁移后仍全 0。
    （默认补齐是注册期语义，升级迁移不得改动业务数据。）"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.1.0", is_default=0)
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=0)

    init_schema(legacy_conn)

    rows = _image_rows(legacy_conn)
    assert len(rows) == 2
    assert all(r["is_default"] == 0 for r in rows)


# ── 二、结构与索引切换（I5） ──────────────────────────────────

def test_migrated_schema_is_name_keyed(legacy_conn):
    """迁移后 image 表列集 = framework_version 消失，framework 可空。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0")
    init_schema(legacy_conn)

    cols = _columns(legacy_conn, "image")
    assert set(cols) == _NEW_IMAGE_COLS
    assert "framework_version" not in cols        # 更名完成
    notnull = {r[1]: r[3] for r in legacy_conn.execute("PRAGMA table_info(image)")}
    assert notnull["name"] == 1                   # name NOT NULL（新主键定位）
    assert notnull["version"] == 1
    assert notnull["framework"] == 0              # 降级为可空展示列


def test_migrated_indexes_swapped(legacy_conn):
    """旧 fw 索引退役、name 索引接管（launch-spec / 查询走新索引）。"""
    init_schema(legacy_conn)
    idx = _image_indexes(legacy_conn)
    assert {"idx_image_fw", "idx_image_fw_ver"}.isdisjoint(idx)  # 旧索引清理
    assert _NEW_IMAGE_INDEXES <= idx                              # 新索引就位


# ── 三、迁移后业务语义 ───────────────

def test_default_version_lookup_by_name_after_migration(legacy_conn):
    """launch-spec 默认版本路径可用：按 name + is_default=1 命中迁移行。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    _seed_legacy_image(legacy_conn, "opencode", "v0.1.0")
    init_schema(legacy_conn)

    row = legacy_conn.execute(
        "SELECT version FROM image WHERE registry=? AND name=? AND is_default=1",
        (IMG_REG, "opencode"),
    ).fetchone()
    assert row is not None
    assert row[0] == "v0.2.0"


def test_version_ordering_after_migration(legacy_conn):
    """排序契约保持：ORDER BY version_key DESC，新版本在前（v0.10.0 > v0.2.0）。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0")
    _seed_legacy_image(legacy_conn, "opencode", "v0.10.0")
    _seed_legacy_image(legacy_conn, "opencode", "v0.1.0")
    init_schema(legacy_conn)

    versions = [
        r[0] for r in legacy_conn.execute(
            "SELECT version FROM image WHERE name='opencode'"
            " ORDER BY version_key DESC"
        )
    ]
    assert versions == ["v0.10.0", "v0.2.0", "v0.1.0"]


def test_service_id_stable_re_register_upserts(legacy_conn):
    """I3 主键稳定：迁移后按新契约（name+version）重注册同一镜像 ->
    幂等 upsert 而非新增行；is_default / created_at 保留。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    init_schema(legacy_conn)

    backend = Backend("memory", legacy_conn)
    table_svc = RegistryTableService(backend)
    image_svc = ImageService(table_svc)

    result = image_svc.register_image(
        name="opencode",
        version="v0.2.0",
        runtime_spec={"rootfs": {"imageurl": "harbor.local/adapted/opencode:v0.2.0"}},
        uploaded_by="user-02",
    )
    assert result["status"] == "updated"          # 命中迁移行，非重复注册
    rows, total = image_svc.query(name="opencode")
    assert total == 1                             # 不产生第二行
    assert rows[0]["is_default"] is True          # 默认标志保留
    assert rows[0]["created_at"] == "2026-06-01T00:00:00Z"  # 旧时间戳保留
    assert rows[0]["uploaded_by"] == "user-02"    # upsert 覆盖为新的上传者


# ── 四、全新库与幂等（I6 前半） ───────────────────────────────

def test_fresh_database_creates_new_schema():
    """全新库（无 image 表）：直接建新结构，不触发迁移、不报错。"""
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    try:
        init_schema(conn)
        assert _image_rows(conn) == []
        assert set(_columns(conn, "image")) == _NEW_IMAGE_COLS
        assert TEMP_TABLE not in _tables(conn)    # 全新库无临时表
    finally:
        conn.close()


def test_init_schema_idempotent_after_migration(legacy_conn):
    """迁移后重启（重复 init_schema）：快照逐字段一致，无重复行。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    _seed_legacy_image(legacy_conn, "opencode", "v0.1.0")
    init_schema(legacy_conn)
    snapshot = _image_rows(legacy_conn)

    init_schema(legacy_conn)                      # 模拟进程重启再跑一遍

    assert _image_rows(legacy_conn) == snapshot
    assert TEMP_TABLE not in _tables(legacy_conn)


# ── 五、上次迁移失败的残留重入（I6 后半，风险最高） ───────────

def test_reentry_leftover_empty_temp_table(legacy_conn):
    """中断点①：临时表已建、数据未拷贝（CREATE 后即中断）。
    重启应完成迁移：数据正确、以原表为准、清理临时表。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    _create_temp_image_table(legacy_conn)         # 空临时表残留

    init_schema(legacy_conn)                      # 不得抛异常

    rows = _image_rows(legacy_conn)
    assert len(rows) == 1
    assert rows[0]["name"] == "opencode"
    assert rows[0]["version"] == "v0.2.0"
    assert rows[0]["is_default"] == 1
    assert TEMP_TABLE not in _tables(legacy_conn)  # 垃圾清理


def test_reentry_leftover_temp_table_with_partial_data(legacy_conn):
    """中断点②：数据已部分拷入临时表、原表仍在（INSERT 中断）。
    重启后行集 = 原表全量（不重复、不混入临时表旧数据）、临时表清理。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    _seed_legacy_image(legacy_conn, "opencode", "v0.1.0")
    partial_row = (
        IMG_REG, image_sid("opencode", "v0.2.0"), "opencode", "opencode",
        "v0.2.0", version_key("v0.2.0"), 1, "system",
        json.dumps({"runtime_spec": {"rootfs": {"imageurl": "stale:v0.2.0"}}}),
    )
    _create_temp_image_table(legacy_conn, [partial_row])  # 上次只拷了一行

    init_schema(legacy_conn)

    rows = _image_rows(legacy_conn)
    assert len(rows) == 2                          # 以原表为准，不重复
    assert {(r["name"], r["version"]) for r in rows} == {
        ("opencode", "v0.2.0"), ("opencode", "v0.1.0"),
    }
    # 原表数据语义保留（临时表里的陈旧 data 不得覆盖）
    by_ver = {r["version"]: r for r in rows}
    assert (
        json.loads(by_ver["v0.2.0"]["data"])["runtime_spec"]["rootfs"]["imageurl"]
        == "harbor.local/adapted/opencode:v0.2.0"
    )
    assert TEMP_TABLE not in _tables(legacy_conn)


def test_reentry_data_stranded_in_temp_table():
    """中断点③（最危险）：原表已 DROP、RENAME 未执行，数据只存在于临时表。
    重启必须恢复全部数据（零丢失）——升级迁移不允许任何中断点丢行。"""
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    try:
        conn.executescript(
            "CREATE TABLE registry_meta ("
            " registry TEXT PRIMARY KEY, kind TEXT NOT NULL, config TEXT);"
        )
        conn.execute(
            "INSERT INTO registry_meta(registry, kind) VALUES (?, 'image')",
            (IMG_REG,),
        )
        stranded = [
            (
                IMG_REG, image_sid("opencode", "v0.2.0"), "opencode", "opencode",
                "v0.2.0", version_key("v0.2.0"), 1, "system",
                json.dumps({"created_at": "2026-06-01T00:00:00Z"}),
            ),
            (
                IMG_REG, image_sid("llama_index", "0.9.0"), "llama_index",
                "llama_index", "0.9.0", version_key("0.9.0"), 1, "system", "{}",
            ),
        ]
        _create_temp_image_table(conn, stranded)   # 数据只在临时表，无 image 表

        init_schema(conn)                          # 不得抛异常、不得丢数据

        rows = [
            dict(r) for r in conn.execute(
                "SELECT service_id, name, version, is_default"
                " FROM image ORDER BY service_id"
            )
        ]
        assert len(rows) == 2                      # 零丢失
        assert {(r["name"], r["version"]) for r in rows} == {
            ("opencode", "v0.2.0"), ("llama_index", "0.9.0"),
        }
        assert all(r["is_default"] == 1 for r in rows)
        assert TEMP_TABLE not in _tables(conn)
    finally:
        conn.close()


def test_leftover_temp_table_on_already_migrated_db(legacy_conn):
    """已迁移库上残留临时表（历史垃圾）：重启不崩溃、数据不变、垃圾清理。"""
    _seed_legacy_image(legacy_conn, "opencode", "v0.2.0", is_default=1)
    init_schema(legacy_conn)
    snapshot = _image_rows(legacy_conn)
    _create_temp_image_table(legacy_conn)          # 人为制造残留垃圾

    init_schema(legacy_conn)                       # 重启：不得崩溃

    assert _image_rows(legacy_conn) == snapshot    # 数据不变
    assert TEMP_TABLE not in _tables(legacy_conn)  # 残留清理
