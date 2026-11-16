"""Backend 跨线程安全回归测试。

历史缺陷：单个 sqlite3.Connection 被 FastAPI threadpool、心跳 sweeper 与
anti-entropy sweeper 共享，而 ``with conn`` 的事务作用在连接上而非线程上 ——
两个线程会看到彼此未提交的数据，一方回滚会连带丢弃另一方已"提交"的写。
Backend 现在对每条语句持 RLock。
"""
from __future__ import annotations

import threading
import time

from a2x_registry.common.db import Backend, connect, init_schema


class _TrackingConn:
    """Wrap a real sqlite connection and record peak concurrent use."""

    def __init__(self, real):
        self._real = real
        self._guard = threading.Lock()
        self.in_flight = 0
        self.max_in_flight = 0

    def _enter(self) -> None:
        with self._guard:
            self.in_flight += 1
            self.max_in_flight = max(self.max_in_flight, self.in_flight)
        # widen the window so an unsynchronised caller reliably overlaps
        time.sleep(0.002)

    def _leave(self) -> None:
        with self._guard:
            self.in_flight -= 1

    def execute(self, sql, args=()):
        self._enter()
        try:
            return self._real.execute(sql, args)
        finally:
            self._leave()

    def __enter__(self):
        self._real.__enter__()
        return self

    def __exit__(self, *exc_info):
        return self._real.__exit__(*exc_info)


def test_statements_are_serialised_across_threads(tmp_path):
    real = connect({"kind": "sqlite", "path": str(tmp_path / "t.db")}).conn
    init_schema(real)
    tracking = _TrackingConn(real)
    backend = Backend(kind="sqlite", conn=tracking)

    errors: list[BaseException] = []

    def worker(i: int) -> None:
        try:
            for j in range(10):
                backend.execute(
                    "INSERT INTO registry_meta (registry, kind) VALUES (?, ?)",
                    (f"n-{i}-{j}", "service"),
                )
                backend.query("SELECT registry FROM registry_meta LIMIT 5")
        except BaseException as exc:  # noqa: BLE001
            errors.append(exc)

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    assert not errors, errors
    assert tracking.max_in_flight == 1
    rows = backend.query("SELECT COUNT(*) AS c FROM registry_meta")
    assert rows[0]["c"] == 40


def test_connect_sets_a_busy_timeout(tmp_path):
    backend = connect({"kind": "sqlite", "path": str(tmp_path / "t.db")})
    assert backend.conn.execute("PRAGMA busy_timeout").fetchone()[0] >= 30000


def test_memory_backend_is_reusable_from_threads():
    backend = connect({"kind": "memory"})
    init_schema(backend.conn)
    backend.execute("INSERT INTO registry_meta (registry, kind) VALUES (?, ?)", ("a", "service"))
    out: list = []

    t = threading.Thread(
        target=lambda: out.append(backend.query("SELECT registry FROM registry_meta"))
    )
    t.start()
    t.join()

    assert out and out[0][0]["registry"] == "a"
