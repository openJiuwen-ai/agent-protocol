"""Unified SQL backend: schema source of truth + Backend abstraction.

Two storage backends share the same schema and parameterized SQL, selectable
via ``connect(cfg)["kind"]``:

- ``sqlite``  - production single-node, file-persisted (default).
- ``memory``  - debug only, ``sqlite3 ":memory:"``; data lost on process exit.

Business code depends only on ``Backend.execute / query``, never on the kind.
Switching backends changes only ``connect(cfg)``; callers stay unchanged.
"""

from __future__ import annotations

import sqlite3
from typing import Any


# ── schema SQL source of truth ─────────────────────────────────
# Kept in sync with tests/db/schema.sql; the test side imports from here to
# avoid maintaining two copies.

SCHEMA_SQL = """\
-- Agent OS registry table schema (authoritative)
-- Use CREATE TABLE/INDEX IF NOT EXISTS so startup-time creation is idempotent.

-- Registry meta: which named registries exist, their kind, and config
CREATE TABLE IF NOT EXISTS registry_meta (
  registry TEXT PRIMARY KEY,             -- 'toolret'/'publicmcp'/'default'/'image-registry'/'instance-registry'
  kind     TEXT NOT NULL,                -- service | image | instance
  config   TEXT                          -- JSON: service-kind stores register_config/vector_config/taxonomy_hash
);

-- Service (A2X: generic/a2a/skill) -- discovery / classification rely on it
CREATE TABLE IF NOT EXISTS service (
  registry    TEXT NOT NULL,
  service_id  TEXT NOT NULL,
  type        TEXT NOT NULL,             -- generic | a2a | skill
  source      TEXT NOT NULL,             -- user_config | api_config | ephemeral | skill_folder
  name        TEXT,                      -- hot: classification LLM input / filter
  description TEXT,                      -- hot: classification LLM input
  data        TEXT NOT NULL,             -- JSON: service_data / agent_card / skill_data
  created_at  TEXT NOT NULL,
  updated_at  TEXT NOT NULL,
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_service_type ON service(registry, type);

-- Image (one row per version: name 主键, framework 降级为展示列)
CREATE TABLE IF NOT EXISTS image (
  registry          TEXT NOT NULL,
  service_id        TEXT NOT NULL,               -- image_sid(name, version)
  name              TEXT NOT NULL,               -- hot: 镜像主键（取代原 framework 定位）
  framework         TEXT,                        -- 展示字段（非主键），可按其筛选
  version           TEXT NOT NULL,               -- hot: lookup by version（原 framework_version 更名）
  version_key       TEXT NOT NULL,               -- sort: normalized semver key computed at registration (see image/version_key.py)
  is_default        INTEGER NOT NULL DEFAULT 0,  -- default-version flag for a name (exactly one row per name = 1); not part of sort order
  uploaded_by       TEXT,                        -- hot: filter by uploader; pre-seeded entries are 'system'
  data              TEXT NOT NULL,               -- JSON flat (no rootfs wrapper): {runtime_spec, access_mode, env_vars, workspace, mounts, description, package_path, image_archive_path, image_module_version, created_at}
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_image_name    ON image(registry, name);
CREATE INDEX IF NOT EXISTS idx_image_name_ver ON image(registry, name, version);
CREATE INDEX IF NOT EXISTS idx_image_by      ON image(registry, uploaded_by);
CREATE INDEX IF NOT EXISTS idx_image_order   ON image(registry, name, version_key DESC);

-- Instance (status persisted inside data JSON, written by gateway via PATCH)
CREATE TABLE IF NOT EXISTS instance (
  registry          TEXT NOT NULL,
  service_id        TEXT NOT NULL,       -- instance_sid(user, framework)
  kind              TEXT NOT NULL,       -- third-party | jiuwen
  framework         TEXT,
  framework_version TEXT,
  node              TEXT,                -- hot: bulk eviction by node / lookup by node
  "user"            TEXT,                -- hot: lookup a user's instances by user id
  data              TEXT NOT NULL,       -- JSON {address, created_at, last_active_at}
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_instance_node ON instance(registry, node);
CREATE INDEX IF NOT EXISTS idx_instance_fw   ON instance(registry, framework, framework_version);
CREATE INDEX IF NOT EXISTS idx_instance_user ON instance(registry, "user");
CREATE INDEX IF NOT EXISTS idx_instance_order ON instance(registry, framework, "user", service_id);
"""


def init_schema(conn: Any) -> None:
    """Execute SCHEMA_SQL on the given sqlite3 connection.

    Creates the 4 tables + 9 indexes, idempotent (every statement is
    ``CREATE ... IF NOT EXISTS``; re-running neither raises nor alters
    existing structures). Called once by ``backend/startup.py`` at startup;
    works identically for file-backed and ``:memory:`` connections.

    A legacy image table (framework-keyed) is migrated
    first: name=framework / version=framework_version backfill in a
    rebuild (SQLite cannot ALTER COLUMN), preserving every row's
    service_id / is_default / data.
    """
    _migrate_legacy_image_table(conn)
    conn.executescript(SCHEMA_SQL)
    conn.commit()


def _migrate_legacy_image_table(conn: Any) -> None:
    """Migrate a framework-keyed image table to the name-keyed layout.

    Detection: an existing ``image`` table that has ``framework_version``
    but no ``name`` column. The rebuild copies each row with
    ``name=framework`` / ``version=framework_version`` — same values the
    old positioning used, so ``service_id`` (``image_sid`` over the same
    two strings) stays valid. Fresh databases and already-migrated ones
    are left untouched. Must run **before** ``SCHEMA_SQL`` (its new
    indexes reference the ``name``/``version`` columns).
    """
    cols = {
        row[1] for row in conn.execute("PRAGMA table_info(image)").fetchall()
    }
    if not cols or "name" in cols:
        return  # fresh schema or already migrated
    conn.executescript(
        """
        BEGIN;
        CREATE TABLE image_migrated (
          registry          TEXT NOT NULL,
          service_id        TEXT NOT NULL,
          name              TEXT NOT NULL,
          framework         TEXT,
          version           TEXT NOT NULL,
          version_key       TEXT NOT NULL,
          is_default        INTEGER NOT NULL DEFAULT 0,
          uploaded_by       TEXT,
          data              TEXT NOT NULL,
          PRIMARY KEY (registry, service_id)
        );
        INSERT INTO image_migrated
            (registry, service_id, name, framework, version,
             version_key, is_default, uploaded_by, data)
          SELECT registry, service_id, framework, framework,
                 framework_version, version_key, is_default,
                 uploaded_by, data
            FROM image;
        DROP TABLE image;
        ALTER TABLE image_migrated RENAME TO image;
        COMMIT;
        """
    )


# ── Backend abstraction ────────────────────────────────────────
# Both backends share the same ``Backend.execute / query`` contract.
# Callers rely on this abstraction to hide file-persisted sqlite vs
# in-memory debug differences.

_KIND_SQLITE = "sqlite"
_KIND_MEMORY = "memory"


class Backend:
    """Storage backend abstraction.

    - ``execute(sql, args)``: write-transaction semantics. The statement is
      wrapped in ``with conn`` -- success commits, an exception rolls back
      that statement.
    - ``query(sql, args)``: read semantics. Returns ``list[dict]`` with
      column names as keys; empty result returns ``[]``.

    All SQL inputs must be parameterized (``?`` placeholders +
    ``args: tuple``); string-concatenating values is forbidden.
    """

    __slots__ = ("kind", "conn")

    def __init__(self, kind: str, conn: Any) -> None:
        self.kind = kind
        self.conn = conn

    def execute(self, sql: str, args: tuple = ()) -> None:
        """Execute a single write SQL statement.

        Wraps the statement in ``with conn`` so it becomes its own
        transaction (commit on success / rollback on exception).
        """
        with self.conn:                       # commit on success / rollback on exception
            self.conn.execute(sql, args)

    def query(self, sql: str, args: tuple = ()) -> list[dict]:
        """Read query, returns list[dict] (column names as keys, empty result []).

        ``row_factory`` is set to ``sqlite3.Row`` in ``connect``; rows are
        converted to dicts here.
        """
        cur = self.conn.execute(sql, args)
        rows = cur.fetchall()
        return [{k: row[k] for k in row.keys()} for row in rows]


def connect(cfg: dict) -> Backend:
    """Return a Backend according to the config.

    - ``{"kind": "sqlite", "path": "<db file>"}``: returns
      ``Backend(kind="sqlite", conn=sqlite3.Connection)``; the connection sets
      ``row_factory=sqlite3.Row`` for convenient column-name access.
    - ``{"kind": "memory"}``: returns
      ``Backend(kind="memory", conn=sqlite3.Connection)`` backed by
      ``sqlite3.connect(":memory:")``. Debug only - data is lost when the
      process exits. ``check_same_thread=False`` so FastAPI sync routes
      running in a threadpool can share the same in-memory connection.
    - Any other kind: ``ValueError``.
    """
    kind = cfg.get("kind")
    if kind == _KIND_SQLITE:
        path = cfg.get("path")
        if not path:
            raise ValueError("sqlite backend requires cfg['path']")
        # check_same_thread=False: FastAPI runs sync routes in a threadpool;
        # the connection is created in the warmup thread and reused by
        # worker threads. SQLite-level concurrency is serialized via the
        # GIL + the default busy_timeout; cross-thread sharing is safe as
        # long as we don't concurrently mutate (the registry's CRUD is
        # short-lived and the sweeper is the only background writer).
        conn = sqlite3.connect(path, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        return Backend(kind=_KIND_SQLITE, conn=conn)
    if kind == _KIND_MEMORY:
        # check_same_thread=False: FastAPI runs sync routes in a threadpool;
        # the in-memory connection must be shareable across those threads.
        conn = sqlite3.connect(":memory:", check_same_thread=False)
        conn.row_factory = sqlite3.Row
        return Backend(kind=_KIND_MEMORY, conn=conn)
    raise ValueError(f"unknown backend kind: {kind!r}")
