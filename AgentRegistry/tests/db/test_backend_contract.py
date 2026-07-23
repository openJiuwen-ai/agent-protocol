"""common/db.py 契约测试。

三种后端共用同一组断言：
- sqlite  —— 文件持久化（生产默认）
- memory  —— ``:memory:`` 调试
- rqlite  —— Raft 复制（本地单节点 rqlited）

契约要点：
- SCHEMA_SQL / init_schema(conn)      —— schema 真源 + 启动建表（三后端共用）
- connect(cfg) -> Backend             —— kind 选项 sqlite / memory / rqlite
- Backend.kind == "sqlite"|"memory"|"rqlite"
- Backend.execute(sql, args)          —— 单语句事务语义（sqlite/memory）、Raft 提交（rqlite）
- Backend.query(sql, args) -> list[dict] —— 读返回 list[dict]
"""

from __future__ import annotations

import sqlite3

import pytest


# ── SCHEMA_SQL / init_schema：sqlite 驱动验证（schema 真源不变） ───────────

def test_schema_sql_constants_present():
    """源码 SCHEMA_SQL 常量存在且非空。"""
    from a2x_registry.common.db import SCHEMA_SQL
    assert isinstance(SCHEMA_SQL, str)
    assert "CREATE TABLE IF NOT EXISTS registry_meta" in SCHEMA_SQL
    assert "CREATE TABLE IF NOT EXISTS instance" in SCHEMA_SQL


def test_connect_sqlite_usable_across_threads(tmp_path):
    """SQLite 文件后端必须在创建线程之外可用（FastAPI 线程池调用）。

    sqlite3 默认 ``check_same_thread=True``，但注册中心的后端连接在 warmup
    线程创建、被 FastAPI 同步路由的线程池 worker 复用。connect() 必须显式
    关闭该限制，否则首次跨线程访问即 ``ProgrammingError``。
    """
    import threading
    from a2x_registry.common.db import connect

    backend = connect({"kind": "sqlite", "path": str(tmp_path / "t.db")})
    err: list = []
    def _worker():
        try:
            backend.execute("CREATE TABLE t(x INTEGER)")
            backend.execute("INSERT INTO t(x) VALUES (?)", (1,))
            rows = backend.query("SELECT x FROM t")
            assert rows == [{"x": 1}]
        except Exception as exc:  # noqa: BLE001
            err.append(exc)
    t = threading.Thread(target=_worker)
    t.start(); t.join()
    assert not err, f"cross-thread sqlite access failed: {err[0]!r}"


def test_init_schema_creates_four_tables(tmp_path):
    """init_schema(conn) 在空 sqlite 库上建齐 4 表 + 6 索引。"""
    from a2x_registry.common.db import init_schema

    conn = sqlite3.connect(str(tmp_path / "t.db"))
    try:
        init_schema(conn)
        tables = {r[0] for r in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"
        )}
        assert tables == {"registry_meta", "service", "image", "instance"}

        indexes = {r[0] for r in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='index' "
            "AND name NOT LIKE 'sqlite_autoindex%'"
        )}
        assert indexes == {
            "idx_service_type", "idx_image_fw", "idx_image_fw_ver",
            "idx_image_by", "idx_image_order",
            "idx_instance_node", "idx_instance_fw", "idx_instance_user",
            "idx_instance_order",
        }
    finally:
        conn.close()


def test_init_schema_is_idempotent(tmp_path):
    """init_schema 重复调用不报错（CREATE … IF NOT EXISTS 幂等）。"""
    from a2x_registry.common.db import init_schema

    conn = sqlite3.connect(str(tmp_path / "t.db"))
    try:
        init_schema(conn)
        init_schema(conn)   # 重复
        cnt = conn.execute(
            "SELECT count(*) FROM sqlite_master WHERE type='table' "
            "AND name='registry_meta'"
        ).fetchone()[0]
        assert cnt == 1
    finally:
        conn.close()


# ── connect 三后端分支 ─────────────────────────────────────────

def test_connect_returns_sqlite_backend(tmp_path):
    """connect({kind:sqlite}) 返回 Backend(kind='sqlite', conn=sqlite3.Connection)。"""
    from a2x_registry.common import db as db_module
    cfg = {"kind": "sqlite", "path": str(tmp_path / "t.db")}
    backend = db_module.connect(cfg)
    assert backend.kind == "sqlite"
    assert isinstance(backend.conn, sqlite3.Connection)


def test_connect_returns_memory_backend():
    """connect({kind:memory}) 返回 Backend(kind='memory', conn=sqlite3.Connection)。"""
    from a2x_registry.common import db as db_module
    backend = db_module.connect({"kind": "memory"})
    assert backend.kind == "memory"
    assert isinstance(backend.conn, sqlite3.Connection)


def test_connect_returns_rqlite_backend():
    """connect({kind:rqlite}) 返回 Backend(kind='rqlite', conn=RqliteConnection)。"""
    from a2x_registry.common import db as db_module
    from a2x_registry.common.db import RqliteConnection
    backend = db_module.connect(
        {"kind": "rqlite", "endpoint": "http://127.0.0.1:4001"}
    )
    assert backend.kind == "rqlite"
    assert isinstance(backend.conn, RqliteConnection)
    assert backend.conn.endpoint == "http://127.0.0.1:4001"


def test_connect_rqlite_default_endpoint():
    """connect({kind:rqlite}) 不传 endpoint 时默认 http://127.0.0.1:4001。"""
    from a2x_registry.common import db as db_module
    backend = db_module.connect({"kind": "rqlite"})
    assert backend.conn.endpoint == "http://127.0.0.1:4001"


def test_connect_rejects_unknown_kind():
    """未知 kind 抛 ValueError。"""
    from a2x_registry.common import db as db_module
    with pytest.raises(ValueError):
        db_module.connect({"kind": "mongodb"})


def test_connect_sqlite_requires_path():
    """sqlite 后端必须传 path。"""
    from a2x_registry.common import db as db_module
    with pytest.raises(ValueError):
        db_module.connect({"kind": "sqlite"})


# ── 三后端参数化契约 ──────────────────────────────────────────
# 所有三后端都通过 `backend_factory` fixture 提供已 init_schema 的 Backend。
# 每个测试函数跑 3 次（sqlite / memory / rqlite）。


def test_backend_kind_matches_param(backend_factory):
    """backend.kind 与参数化参数一致。"""
    kind, backend = backend_factory
    assert backend.kind == kind


def test_backend_execute_write_and_query_roundtrip(backend_factory):
    """execute(sql, args) 写入后 query 可读到（单语句事务语义）。"""
    _, backend = backend_factory
    backend.execute("CREATE TABLE t(x INTEGER)")
    backend.execute("INSERT INTO t(x) VALUES (?)", (42,))

    rows = backend.query("SELECT x FROM t")
    assert rows == [{"x": 42}]


def test_backend_query_returns_list_of_dict(backend_factory):
    """query 返回 list[dict]，列名作 key。"""
    _, backend = backend_factory
    backend.execute("CREATE TABLE t(a TEXT, b INTEGER)")
    backend.execute("INSERT INTO t(a, b) VALUES (?, ?)", ("hello", 7))

    rows = backend.query("SELECT a, b FROM t")
    assert isinstance(rows, list)
    assert len(rows) == 1
    assert rows[0] == {"a": "hello", "b": 7}


def test_backend_query_empty_result(backend_factory):
    """query 空结果返回 []（不是 None）。"""
    _, backend = backend_factory
    backend.execute("CREATE TABLE t(x INTEGER)")
    rows = backend.query("SELECT x FROM t WHERE x > ?", (100,))
    assert rows == []


def test_backend_execute_rollback_on_exception(backend_factory):
    """execute 抛异常时事务回滚（UNIQUE 冲突不污染已提交行）。"""
    _, backend = backend_factory
    backend.execute("CREATE TABLE t(x INTEGER UNIQUE)")
    backend.execute("INSERT INTO t(x) VALUES (?)", (1,))

    with pytest.raises(Exception):
        backend.execute("INSERT INTO t(x) VALUES (?)", (1,))   # UNIQUE 冲突

    rows = backend.query("SELECT count(*) AS c FROM t")
    assert rows[0]["c"] == 1                          # 回滚，仍只有 1 行


def test_backend_parameterized_args_preserved(backend_factory):
    """多参数化查询的值顺序与类型正确（参数化 SQL 三后端共用）。"""
    _, backend = backend_factory
    backend.execute("CREATE TABLE t(a TEXT, b INTEGER, c REAL)")
    backend.execute(
        "INSERT INTO t(a, b, c) VALUES (?, ?, ?)",
        ("foo", 10, 3.14),
    )
    rows = backend.query("SELECT a, b, c FROM t WHERE a = ? AND b > ?", ("foo", 5))
    assert rows == [{"a": "foo", "b": 10, "c": 3.14}]


# ── init_schema 在三后端上的建表验证 ─────────────────────────

def test_init_schema_creates_four_tables_on_all_backends(backend_factory):
    """init_schema 在三后端上都建齐 4 表。"""
    from a2x_registry.common.db import RqliteConnection

    _, backend = backend_factory
    if isinstance(backend.conn, RqliteConnection):
        # rqlite: 用 sqlite_master 查表
        rows = backend.query(
            "SELECT name FROM sqlite_master WHERE type='table'"
        )
    else:
        rows = backend.query(
            "SELECT name FROM sqlite_master WHERE type='table'"
        )
    tables = {r["name"] for r in rows}
    assert {"registry_meta", "service", "image", "instance"}.issubset(tables)


# ── memory 后端特有：不持久化、跨线程共享 ────────────────────

def test_memory_backend_not_persisted():
    """memory 后端每次 connect 都是新空库（进程退出即丢语义）。"""
    from a2x_registry.common import db as db_module

    b1 = db_module.connect({"kind": "memory"})
    b1.execute("CREATE TABLE t(x INTEGER)")
    b1.execute("INSERT INTO t(x) VALUES (?)", (1,))
    assert b1.query("SELECT x FROM t") == [{"x": 1}]

    # 第二次 connect 是独立连接，看不到上一次的数据
    b2 = db_module.connect({"kind": "memory"})
    with pytest.raises(Exception):
        b2.query("SELECT x FROM t")
