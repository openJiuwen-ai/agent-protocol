"""tests/db 数据层测试共享 fixtures。

两种后端契约测试 fixtures：sqlite（tmp 文件）、memory（``:memory:``）。
所有后端共用同一份 SCHEMA_SQL + 参数化 SQL，契约测试在两种后端上跑同一组断言。

预制 .db 文件由 `build_fixtures.sh` 用 sqlite3 CLI 生成（schema 真源来自
`a2x_registry/common/db.py` 的 `SCHEMA_SQL`）：
- `fixtures/empty.db`     -- 仅 schema，无数据
- `fixtures/appliance.db` -- schema + appliance 样例数据（只读）
"""

from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Iterator

import pytest

FIXTURES_DIR = Path(__file__).parent / "fixtures"


# ── 预制 .db 文件路径（只读） ─────────────────────────────────

@pytest.fixture
def empty_db_path() -> Path:
    """预制 empty.db 路径（仅 schema，无数据）。"""
    p = FIXTURES_DIR / "empty.db"
    assert p.exists(), (
        f"{p} 不存在；请运行 `bash tests/db/build_fixtures.sh` 重新生成"
    )
    return p


@pytest.fixture
def appliance_db_path() -> Path:
    """预制 appliance.db 路径（schema + 样例数据，只读校验用）。"""
    p = FIXTURES_DIR / "appliance.db"
    assert p.exists(), (
        f"{p} 不存在；请运行 `bash tests/db/build_fixtures.sh` 重新生成"
    )
    return p


# ── 只读连接（校验预制 fixture） ──────────────────────────────

@pytest.fixture
def appliance_conn(appliance_db_path) -> Iterator[sqlite3.Connection]:
    """只读连接到 appliance.db；测试结束自动关闭。"""
    conn = sqlite3.connect(
        f"file:{appliance_db_path}?mode=ro", uri=True
    )
    conn.row_factory = sqlite3.Row
    try:
        yield conn
    finally:
        conn.close()


# ── 可写连接（隔离 CRUD 测试，不污染预制 fixture） ────────────

@pytest.fixture
def fresh_conn(tmp_path) -> Iterator[sqlite3.Connection]:
    """全新空 db（tmp_path），按源码 init_schema 建齐 4 表。

    复用 `a2x_registry.common.db.init_schema` -- 测试与源码共用同一份
    SCHEMA_SQL 真源，避免漂移。每个 CRUD 测试拿到的都是干净库，互不影响；
    测试结束 tmp_path 自动清理。
    """
    from a2x_registry.common.db import init_schema

    db_path = tmp_path / "test.db"
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    init_schema(conn)
    try:
        yield conn
    finally:
        conn.close()


@pytest.fixture
def appliance_writable_copy(tmp_path, appliance_db_path) -> Iterator[sqlite3.Connection]:
    """appliance.db 的可写副本（tmp_path）-- 需要在样例数据上做变更测试时用。

    原始 fixture 保持只读；本 fixture 先复制到 tmp_path 再开可写连接。
    """
    import shutil
    dst = tmp_path / "appliance_copy.db"
    shutil.copy2(appliance_db_path, dst)
    conn = sqlite3.connect(str(dst))
    conn.row_factory = sqlite3.Row
    try:
        yield conn
    finally:
        conn.close()


# ── 双后端契约测试 fixtures ──────────────────────────────────
# sqlite / memory 两个 fixture 共用同一组测试断言（参数化 via
# `backend_factory`），共享同一份 SCHEMA_SQL。


@pytest.fixture
def sqlite_backend(tmp_path):
    """Fresh sqlite file backend with schema initialized."""
    from a2x_registry.common.db import connect, init_schema

    backend = connect({"kind": "sqlite", "path": str(tmp_path / "t.db")})
    init_schema(backend.conn)
    return backend


@pytest.fixture
def memory_backend():
    """In-memory sqlite backend with schema initialized (debug only)."""
    from a2x_registry.common.db import connect, init_schema

    backend = connect({"kind": "memory"})
    init_schema(backend.conn)
    return backend


@pytest.fixture(params=["sqlite", "memory"])
def backend_factory(request):
    """Parametrized factory yielding (kind_name, backend) tuples.

    Each test runs once per backend kind.
    """
    if request.param == "sqlite":
        backend = request.getfixturevalue("sqlite_backend")
    elif request.param == "memory":
        backend = request.getfixturevalue("memory_backend")
    else:
        pytest.fail(f"unknown backend param: {request.param}")
    return request.param, backend
