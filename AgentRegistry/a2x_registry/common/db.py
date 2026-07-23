"""Unified SQL backend: schema source of truth + Backend abstraction.

Three storage backends share the same schema and parameterized SQL, selectable
via ``connect(cfg)["kind"]``:

- ``sqlite``  — production single-node, file-persisted (default).
- ``rqlite``  — Raft-replicated cluster; HTTP API via stdlib ``urllib`` (no
                third-party dependency). Single-node rqlite is also usable
                for development.
- ``memory``  — debug only, ``sqlite3 ":memory:"``; data lost on process exit.

Business code depends only on ``Backend.execute / query``, never on the kind.
Switching backends changes only ``connect(cfg)``; callers stay unchanged.
"""

from __future__ import annotations

import base64
import json
import re
import sqlite3
import urllib.error
import urllib.request
from typing import Any


# ── schema SQL source of truth ─────────────────────────────────
# Kept in sync with tests/db/schema.sql; the test side imports from here to
# avoid maintaining two copies. rqlite will replicate the same SQL via Raft.

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

-- Image (one row per version)
CREATE TABLE IF NOT EXISTS image (
  registry          TEXT NOT NULL,
  service_id        TEXT NOT NULL,               -- image_sid(framework, framework_version)
  framework         TEXT NOT NULL,               -- hot: lookup by framework
  framework_version TEXT NOT NULL,               -- hot: lookup by version
  version_key       TEXT NOT NULL,               -- sort: normalized semver key computed at registration (see image/version_key.py)
  is_default        INTEGER NOT NULL DEFAULT 0,  -- default-version flag for a framework (exactly one row per framework = 1); not part of sort order
  uploaded_by       TEXT,                        -- hot: filter by uploader; pre-seeded entries are 'system'
  data              TEXT NOT NULL,               -- JSON flat (no rootfs wrapper): {imageurl, workdir, mounts, cpu, memory, ports, env, image_module_version, created_at}
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_image_fw     ON image(registry, framework);
CREATE INDEX IF NOT EXISTS idx_image_fw_ver ON image(registry, framework, framework_version);
CREATE INDEX IF NOT EXISTS idx_image_by     ON image(registry, uploaded_by);
CREATE INDEX IF NOT EXISTS idx_image_order  ON image(registry, framework, version_key DESC);

-- Instance (status is not persisted; derived from node heartbeat at query time)
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
    """Execute SCHEMA_SQL on the given connection (create 4 tables + 6 indexes, idempotent).

    Called once by `backend/startup.py` at startup for all three backends. For
    ``sqlite3.Connection`` uses ``executescript``; for ``RqliteConnection``
    splits SCHEMA_SQL into individual statements (rqlite HTTP API does not
    accept multi-statement scripts in a single call) and executes them one by
    one. Memory mode reuses the sqlite3 path on an ``:memory:`` connection.

    Statement splitting for rqlite: SQL line comments (``-- ...``) may contain
    semicolons, so a naive ``split(';')`` would break. We strip ``--`` comments
    first (the schema is pure DDL with no string literals, so this is safe),
    then split on ``;``.

    Idempotency: every statement is ``CREATE ... IF NOT EXISTS``; re-running it
    does not raise and does not alter existing structures.
    """
    if isinstance(conn, RqliteConnection):
        # Strip SQL line comments (-- to end of line) before splitting on ';'.
        # SCHEMA_SQL is pure DDL (no string literals), so comment stripping
        # cannot accidentally remove real content.
        cleaned = re.sub(r"--[^\n]*", "", SCHEMA_SQL)
        for stmt in cleaned.split(";"):
            stmt = stmt.strip()
            if stmt:
                conn.execute(stmt)
        return
    conn.executescript(SCHEMA_SQL)
    conn.commit()


# ── Backend abstraction ────────────────────────────────────────
# Three backends share the same ``Backend.execute / query`` contract.
# Callers rely on this abstraction to hide single-node sqlite / multi-node
# rqlite / in-memory debug differences.

_KIND_SQLITE = "sqlite"
_KIND_RQLITE = "rqlite"
_KIND_MEMORY = "memory"


def _sql_quote(value: Any) -> str:
    """Render a Python value as a SQLite literal (for rqlite inline use only).

    rqlite v10's HTTP parameter binding is unreliable (string/float/NULL args
    are rejected with ``unsupported type``; integer args are stored as BLOBs).
    To keep the Backend contract uniform we inline-escape args into the SQL
    text on the rqlite path. SQLite's quoting rules are simple and we control
    all call sites (no user-supplied SQL), so this is safe here.
    """
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        # NaN / inf are not representable as SQLite REAL; map to NULL.
        if value != value or value in (float("inf"), float("-inf")):
            return "NULL"
        return repr(value)
    if isinstance(value, bytes):
        return "X'" + value.hex() + "'"
    # str (and fallback for any other type)
    return "'" + str(value).replace("'", "''") + "'"


def _inline_sql(sql: str, args: tuple) -> str:
    """Substitute ``?`` placeholders in ``sql`` with quoted literals.

    Returns ``sql`` unchanged when ``args`` is empty (e.g. DDL from
    ``init_schema``). Raises ``ValueError`` if the placeholder count does not
    match ``len(args)``. Only ``?`` placeholders are supported (no ``?1`` /
    ``:name``); all call sites in this codebase use plain ``?``.
    """
    if not args:
        return sql
    parts = sql.split("?")
    if len(parts) - 1 != len(args):
        raise ValueError(
            f"placeholder count ({len(parts) - 1}) != args count ({len(args)})"
        )
    out = parts[0]
    for i, arg in enumerate(args):
        out += _sql_quote(arg) + parts[i + 1]
    return out


class RqliteConnection:
    """rqlite HTTP connection (stdlib urllib, no third-party deps).

    rqlite does not support multi-statement transactions; each execute call
    is its own Raft commit. The methods here map directly to rqlite's HTTP
    API (``POST /db/execute`` and ``POST /db/query``).

    Parameter handling: rqlite v10's server-side parameter binding is broken
    for non-integer types (see ``_sql_quote``), so we inline-escape args into
    the SQL text on the client side and send plain SQL strings. The
    ``Backend`` contract (``?`` placeholders + ``args`` tuple) is preserved;
    callers are unaware of this difference.

    The connection is stateless from the client side — only the endpoint,
    optional basic-auth, and timeout are kept. Multiple Backends may share
    one endpoint; rqlite routes writes to the leader internally.
    """

    __slots__ = ("endpoint", "auth", "timeout")

    def __init__(
        self,
        endpoint: str,
        auth: str = "",
        timeout: float = 30.0,
    ) -> None:
        self.endpoint = endpoint.rstrip("/")
        self.auth = auth
        self.timeout = timeout

    def _request(self, path: str, body: list) -> dict:
        """Send a JSON POST and return the parsed response body.

        Raises ``RuntimeError`` if rqlite returns an HTTP error status. A
        per-statement ``error`` field in the response is checked by the
        caller (``execute`` / ``query``). Network errors propagate as
        ``urllib.error.URLError``.
        """
        url = f"{self.endpoint}{path}"
        data = json.dumps(body).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=data,
            method="POST",
            headers={"Content-Type": "application/json"},
        )
        if self.auth:
            token = base64.b64encode(self.auth.encode("utf-8")).decode("ascii")
            req.add_header("Authorization", f"Basic {token}")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            body_text = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(
                f"rqlite HTTP {exc.code} from {path}: {body_text}"
            ) from exc
        return payload

    def execute(self, sql: str, args: tuple = ()) -> None:
        """Execute a single write statement (one Raft commit).

        Args are client-side inline-escaped (see class docstring) and the SQL
        is sent as ``[[sql_text]]`` to ``/db/execute``. One statement per call
        mirrors the sqlite path's single-statement transaction semantics.
        """
        stmt = _inline_sql(sql, args)
        payload = self._request("/db/execute", [[stmt]])
        results = payload.get("results", [])
        if results and isinstance(results[0], dict) and "error" in results[0]:
            raise RuntimeError(
                f"rqlite execute error: {results[0]['error']}"
            )

    def query(self, sql: str, args: tuple = ()) -> list[dict]:
        """Read query, returns list[dict] (column names as keys, empty result []).

        Args are client-side inline-escaped (see class docstring). The request
        uses ``?associative&str`` so rqlite returns rows as a list of
        ``{column: value}`` objects with native JSON types (text/integer/real
        come back as str/int/float; BLOBs come back base64-encoded, but this
        schema has no BLOB columns). Body is ``[sql_text]`` (single statement).
        """
        stmt = _inline_sql(sql, args)
        payload = self._request("/db/query?associative&str", [stmt])
        results = payload.get("results", [])
        if not results:
            return []
        r = results[0]
        if isinstance(r, dict) and "error" in r:
            raise RuntimeError(f"rqlite query error: {r['error']}")
        return r.get("rows", []) or []


class Backend:
    """Storage backend abstraction.

    - ``execute(sql, args)``: write-transaction semantics. The SQLite / memory
      implementation wraps a single statement in ``with conn`` -- success
      commits, an exception rolls back that statement. The rqlite path issues
      one HTTP ``/db/execute`` call (one Raft commit per write).
    - ``query(sql, args)``: read semantics. SQLite / memory queries the local
      connection; rqlite issues ``/db/query`` (read from any replica, by
      default eventually consistent). Returns ``list[dict]`` with column names
      as keys; empty result returns ``[]``.

    All SQL inputs must be parameterized (``?`` placeholders +
    ``args: tuple``); string-concatenating values is forbidden.
    """

    __slots__ = ("kind", "conn")

    def __init__(self, kind: str, conn: Any) -> None:
        self.kind = kind
        self.conn = conn

    def execute(self, sql: str, args: tuple = ()) -> None:
        """Execute a single write SQL statement.

        SQLite / memory: wraps the statement in ``with conn`` so it becomes its
        own transaction (commit on success / rollback on exception). rqlite:
        delegates to ``RqliteConnection.execute`` which sends one HTTP request
        (one Raft commit). Each write commits independently, avoiding
        long-held locks and matching rqlite's "one commit per write" semantics.
        """
        if isinstance(self.conn, RqliteConnection):
            self.conn.execute(sql, args)
            return
        with self.conn:                       # commit on success / rollback on exception
            self.conn.execute(sql, args)

    def query(self, sql: str, args: tuple = ()) -> list[dict]:
        """Read query, returns list[dict] (column names as keys, empty result []).

        SQLite / memory: ``row_factory`` is set to ``sqlite3.Row`` in
        ``connect``; rows are converted to dicts here. rqlite: delegates to
        ``RqliteConnection.query`` which returns the same shape. Callers do
        not branch on backend kind.
        """
        if isinstance(self.conn, RqliteConnection):
            return self.conn.query(sql, args)
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
      ``sqlite3.connect(":memory:")``. Debug only — data is lost when the
      process exits. ``check_same_thread=False`` so FastAPI sync routes
      running in a threadpool can share the same in-memory connection.
    - ``{"kind": "rqlite", "endpoint": "<url>", "auth": "<user:pwd>"?}``:
      returns ``Backend(kind="rqlite", conn=RqliteConnection)``. ``endpoint``
      defaults to ``http://127.0.0.1:4001``. Single-node rqlite works for
      development; multi-node requires the rqlite cluster to be bootstrapped
      separately (not the registry's concern).
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
    if kind == _KIND_RQLITE:
        endpoint = cfg.get("endpoint") or "http://127.0.0.1:4001"
        auth = cfg.get("auth", "") or ""
        timeout = float(cfg.get("timeout", 30.0))
        conn = RqliteConnection(endpoint=endpoint, auth=auth, timeout=timeout)
        return Backend(kind=_KIND_RQLITE, conn=conn)
    raise ValueError(f"unknown backend kind: {kind!r}")
