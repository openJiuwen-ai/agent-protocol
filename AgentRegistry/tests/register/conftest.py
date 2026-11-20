"""tests/register 共享 fixtures。

为新的命名注册表通用 CRUD（RegistryTableService）提供隔离的 SQLite
Backend + 已建好 schema 的干净库。每个测试拿到的都是独立 tmp_path 库，
互不污染；测试结束自动清理。
"""

from __future__ import annotations

from typing import Iterator

import pytest

from a2x_registry.common.db import connect, init_schema


@pytest.fixture
def fresh_backend(tmp_path):
    """全新空库 Backend（sqlite），schema 已建齐 4 表 6 索引。"""
    db_path = tmp_path / "register_test.db"
    backend = connect({"kind": "sqlite", "path": str(db_path)})
    init_schema(backend.conn)
    return backend


@pytest.fixture
def table_service(fresh_backend):
    """RegistryTableService 实例，绑定 fresh_backend。"""
    from a2x_registry.register.service import RegistryTableService

    return RegistryTableService(fresh_backend)
