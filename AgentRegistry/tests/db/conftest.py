"""tests/db 数据层测试共享 fixtures。

三种后端契约测试 fixtures：sqlite（tmp 文件）、memory（``:memory:``）、
rqlite（启动临时 rqlited 进程）。所有后端共用同一份 SCHEMA_SQL + 参数化 SQL，
契约测试在三种后端上跑同一组断言。

预制 .db 文件由 `build_fixtures.sh` 用 sqlite3 CLI 生成（schema 真源来自
`a2x_registry/common/db.py` 的 `SCHEMA_SQL`）：
- `fixtures/empty.db`     —— 仅 schema，无数据
- `fixtures/appliance.db` —— schema + appliance 样例数据（只读）
"""

from __future__ import annotations

import json
import shutil
import socket
import sqlite3
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Iterator

import pytest

FIXTURES_DIR = Path(__file__).parent / "fixtures"

# ── Docker rqlite 集群端点（3 节点） ──────────────────────────
# 对应 build_test/rqliteAutomaticClustering/compose.yaml
# HTTP 端口映射到宿主：node-1=4001, node-2=4011, node-3=4021
RQLITE_DOCKER_ENDPOINTS = (
    "http://127.0.0.1:4001",
    "http://127.0.0.1:4011",
    "http://127.0.0.1:4021",
)


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

    复用 `a2x_registry.common.db.init_schema` —— 测试与源码共用同一份
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
    """appliance.db 的可写副本（tmp_path）—— 需要在样例数据上做变更测试时用。

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


# ── 三后端契约测试 fixtures ──────────────────────────────────
# sqlite / memory / rqlite 三个 fixture 共用同一组测试断言（参数化 via
# `backend_factory`），共享同一份 SCHEMA_SQL。rqlite 后端连接已运行的
# Docker 3 节点集群（build_test/rqliteAutomaticClustering/compose.yaml），
# session 范围内所有测试复用同一个集群，通过 leader 端点写入。


def _find_rqlite_leader(endpoints: tuple[str, ...]) -> str | None:
    """遍历端点查 /status，返回当前 leader 的 HTTP 端点。

    rqlite v10 的 /status 返回 ``store.leader`` 为 dict
    (``{"addr": "<raft-addr>", "node_id": "..."}``)，``store.addr`` 为本节点
    raft 地址字符串。比较 ``leader["addr"] == store.addr`` 判断本节点是否
    leader。Docker 集群的 raft 地址用容器 hostname（myrqlite-host-N:4002），
    从宿主无法解析，但比较字符串即可判断 leader 归属。

    兼容旧版 rqlite（leader 为字符串）。
    """
    for ep in endpoints:
        try:
            with urllib.request.urlopen(f"{ep}/status", timeout=2) as r:
                status = json.loads(r.read().decode("utf-8"))
                store = status.get("store", {})
                leader = store.get("leader")
                this_raft = store.get("addr", "")
                if isinstance(leader, dict):
                    leader_raft = leader.get("addr", "")
                else:
                    leader_raft = leader or ""
                if leader_raft and this_raft and leader_raft == this_raft:
                    return ep
        except (urllib.error.URLError, OSError, json.JSONDecodeError):
            continue
    return None


@pytest.fixture(scope="session")
def docker_rqlite_cluster() -> Iterator[str]:
    """连接已运行的 Docker rqlite 3 节点集群，yield leader 的 HTTP 端点。

    前提：``build_test/rqliteAutomaticClustering`` 下的 compose.yaml 已启动
   （``docker compose up -d``）。若集群未运行或选主未完成则 skip。
    """
    leader = _find_rqlite_leader(RQLITE_DOCKER_ENDPOINTS)
    if leader is None:
        pytest.skip(
            "Docker rqlite 集群未运行或无 leader；"
            "请先执行 cd build_test/rqliteAutomaticClustering && docker compose up -d"
        )
    yield leader


@pytest.fixture(scope="session")
def docker_rqlite_all_nodes() -> Iterator[tuple[str, ...]]:
    """返回全部 3 个节点的 HTTP 端点（含 leader + followers），供多节点读复制验证。

    若集群未运行则 skip。
    """
    leader = _find_rqlite_leader(RQLITE_DOCKER_ENDPOINTS)
    if leader is None:
        pytest.skip("Docker rqlite 集群未运行")
    yield RQLITE_DOCKER_ENDPOINTS


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


@pytest.fixture
def rqlite_backend(docker_rqlite_cluster):
    """rqlite backend (Docker 集群 leader) with schema initialized; tables dropped on teardown.

    Re-initializes schema before each test (CREATE ... IF NOT EXISTS is
    idempotent). On teardown DROPs *every* user table (not just the 4 schema
    tables) because rqlite 集群是 session 级共享的——测试常建临时表如 ``t``，
    须在 teardown 清掉以免泄漏到下一个测试。
    """
    from a2x_registry.common.db import connect, init_schema

    backend = connect({"kind": "rqlite", "endpoint": docker_rqlite_cluster})
    init_schema(backend.conn)
    yield backend
    rows = backend.query("SELECT name FROM sqlite_master WHERE type='table'")
    for row in rows:
        backend.execute(f'DROP TABLE IF EXISTS "{row["name"]}"')


def _seed_appliance_data(backend) -> None:
    """将 seed_appliance.sql 的样例数据导入 backend。

    与 init_schema 处理 rqlite 的方式一致：先去掉 ``--`` 行注释，再按 ``;``
    拆分为单语句逐条 execute（rqlite HTTP API 不支持多语句脚本）。
    """
    import re
    seed_path = Path(__file__).parent / "seed_appliance.sql"
    sql_text = seed_path.read_text(encoding="utf-8")
    cleaned = re.sub(r"--[^\n]*", "", sql_text)
    for stmt in cleaned.split(";"):
        stmt = stmt.strip()
        if stmt:
            backend.execute(stmt)


@pytest.fixture
def rqlite_seeded_backend(docker_rqlite_cluster):
    """rqlite backend with schema + appliance 样例数据；teardown 清表。

    供需要预置数据（镜像多版本 / 实例多节点）的 rqlite 测试用，
    等价于 sqlite 测试里的 ``appliance_conn`` / ``appliance_writable_copy``。
    """
    from a2x_registry.common.db import connect, init_schema

    backend = connect({"kind": "rqlite", "endpoint": docker_rqlite_cluster})
    init_schema(backend.conn)
    _seed_appliance_data(backend)
    yield backend
    rows = backend.query("SELECT name FROM sqlite_master WHERE type='table'")
    for row in rows:
        backend.execute(f'DROP TABLE IF EXISTS "{row["name"]}"')


@pytest.fixture(params=["sqlite", "memory", "rqlite"])
def backend_factory(request):
    """Parametrized factory yielding (kind_name, backend) tuples.

    Each test runs once per backend kind. rqlite is skipped if rqlited is not
    installed (handled inside the rqlite_backend fixture).
    """
    if request.param == "sqlite":
        backend = request.getfixturevalue("sqlite_backend")
    elif request.param == "memory":
        backend = request.getfixturevalue("memory_backend")
    elif request.param == "rqlite":
        backend = request.getfixturevalue("rqlite_backend")
    else:
        pytest.fail(f"unknown backend param: {request.param}")
    return request.param, backend
