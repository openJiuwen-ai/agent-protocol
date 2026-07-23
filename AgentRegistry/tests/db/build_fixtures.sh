#!/usr/bin/env bash
# 预制 tests/db/fixtures/*.db —— 用 sqlite3 CLI + 源码 SCHEMA_SQL + seed 数据生成。
#
# 产物：
#   fixtures/empty.db      —— 仅 schema，无数据（供 schema 校验、CRUD 写入隔离测试）
#   fixtures/appliance.db  —— schema + appliance 样例数据（供只读校验：镜像分组、实例过滤等）
#
# schema 真源在 a2x_registry/common/db.py 的 SCHEMA_SQL；本脚本先把它导出为
# tests/db/schema.sql（构建用），再用 sqlite3 CLI 建库。
#
# 重新生成：bash tests/db/build_fixtures.sh
# 依赖：sqlite3 CLI（机器已安装 3.46.1）+ 项目 .venv（用其 python 导出 SCHEMA_SQL）。
set -euo pipefail

cd "$(dirname "$0")/../.."   # 项目根

SQLITE="$(command -v sqlite3 || true)"
if [ -z "$SQLITE" ]; then
  echo "ERROR: sqlite3 CLI not found on PATH" >&2
  exit 1
fi

PYTHON="${PYTHON:-.venv/bin/python}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || command -v python)"
fi

# ── 从源码导出 SCHEMA_SQL → tests/db/schema.sql ─────────────
"$PYTHON" - <<'PY' > tests/db/schema.sql
from a2x_registry.common.db import SCHEMA_SQL
print("-- 一体机 Agent OS 注册中心 · 表结构（构建预制 .db 用）")
print("-- 真源在 a2x_registry/common/db.py 的 SCHEMA_SQL；本文件由 build_fixtures.sh 自动生成。")
print("--    请勿直接编辑本文件；改 schema 请改 a2x_registry/common/db.py。")
print()
print(SCHEMA_SQL, end="")
PY
echo "[ok] tests/db/schema.sql  (从 a2x_registry/common/db.py 导出)"

cd tests/db
mkdir -p fixtures
rm -f fixtures/empty.db fixtures/appliance.db

# empty.db：仅 schema
"$SQLITE" fixtures/empty.db < schema.sql
echo "[ok] fixtures/empty.db  ($("$SQLITE" fixtures/empty.db "SELECT count(*) FROM sqlite_master WHERE type='table';") tables)"

# appliance.db：schema + 样例数据
"$SQLITE" fixtures/appliance.db < schema.sql
"$SQLITE" fixtures/appliance.db < seed_appliance.sql
echo "[ok] fixtures/appliance.db (registry_meta=$("$SQLITE" fixtures/appliance.db 'SELECT count(*) FROM registry_meta;'), " \
     "image=$("$SQLITE" fixtures/appliance.db 'SELECT count(*) FROM image;'), " \
     "instance=$("$SQLITE" fixtures/appliance.db 'SELECT count(*) FROM instance;'))"
