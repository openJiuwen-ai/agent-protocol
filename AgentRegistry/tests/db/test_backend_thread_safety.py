"""Backend 跨线程安全回归测试。

历史缺陷：单个 sqlite3.Connection 被 FastAPI threadpool、心跳 sweeper 与
anti-entropy sweeper 共享，而 ``with conn`` 的事务作用在连接上而非线程上 ——
两个线程会看到彼此未提交的数据，一方回滚会连带丢弃另一方已"提交"的写。
Backend 现在对每条语句持 RLock。

锁是否真的生效用"可控阻塞"来判定，而不是靠固定 sleep 制造概率性重叠：
让第一个进入连接的调用阻塞在一个事件上，再提交第二个调用 —— 有锁则第二个
调用必然阻塞（future 超时），没有锁则它直接跑完（future 正常返回）。这个
判定不依赖负载，去掉锁立刻失败。
"""
from __future__ import annotations

import threading
from concurrent.futures import ThreadPoolExecutor
from concurrent.futures import TimeoutError as FutureTimeout

import pytest

from a2x_registry.common.db import Backend, connect, init_schema

_INSERT = "INSERT INTO registry_meta (registry, kind) VALUES (?, ?)"
_SELECT = "SELECT registry FROM registry_meta LIMIT 5"
_SELECT_ARGS = "SELECT registry FROM registry_meta WHERE registry = ?"


class _ControlledConn:
    """Proxy a real connection; the first ``execute`` call blocks until released.

    Only the *first* statement blocks, so a second caller that is not held back
    by the backend lock completes immediately instead of also waiting.
    """

    def __init__(self, real):
        self._real = real
        self._guard = threading.Lock()
        self.calls = 0
        self.in_flight = 0
        self.max_in_flight = 0
        self.first_entered = threading.Event()
        self.release = threading.Event()

    def _enter(self):
        with self._guard:
            self.calls += 1
            first = self.calls == 1
            self.in_flight += 1
            self.max_in_flight = max(self.max_in_flight, self.in_flight)
        return first

    def _leave(self):
        with self._guard:
            self.in_flight -= 1

    def execute(self, sql, args=()):
        first = self._enter()
        try:
            if first:
                self.first_entered.set()
                assert self.release.wait(timeout=10), "test did not release the first statement"
            return self._real.execute(sql, args)
        finally:
            self._leave()

    def __enter__(self):
        self._real.__enter__()
        return self

    def __exit__(self, *exc_info):
        return self._real.__exit__(*exc_info)


def _backend_with_control(tmp_path):
    real = connect({"kind": "sqlite", "path": str(tmp_path / "t.db")}).conn
    init_schema(real)
    controlled = _ControlledConn(real)
    return Backend(kind="sqlite", conn=controlled), controlled


@pytest.mark.parametrize("second_call", ["execute", "query", "query_with_args"])
def test_lock_serialises_a_second_statement(tmp_path, second_call):
    """第一个语句卡在连接里时，第二个语句必须被 Backend 锁挡住。"""
    backend, conn = _backend_with_control(tmp_path)

    def run_second():
        if second_call == "execute":
            return backend.execute(_INSERT, ("n-b", "service"))
        if second_call == "query_with_args":
            return backend.query(_SELECT_ARGS, ("n-a",))
        return backend.query(_SELECT)

    with ThreadPoolExecutor(max_workers=2) as pool:
        first = pool.submit(backend.execute, _INSERT, ("n-a", "service"))
        assert conn.first_entered.wait(timeout=5), "first statement never reached the connection"

        second = pool.submit(run_second)

        with pytest.raises(FutureTimeout):
            # 无锁时这一句会立刻跑完；有锁时它必然还在等
            second.result(timeout=0.3)

        conn.release.set()
        first.result(timeout=5)
        second.result(timeout=5)

    assert conn.max_in_flight == 1, "two statements overlapped on the shared connection"
    rows = backend.query("SELECT COUNT(*) AS c FROM registry_meta")
    assert rows[0]["c"] == (2 if second_call == "execute" else 1)


def test_barrier_started_mixed_read_write_keeps_rows_consistent(tmp_path):
    """4 个线程用 barrier 同步起跑，混合 execute/query，语句不得重叠。"""
    backend, conn = _backend_with_control(tmp_path)
    conn.release.set()  # 这个用例不需要阻塞第一个语句

    start = threading.Barrier(4)
    errors: list[BaseException] = []

    def worker(i: int) -> None:
        try:
            start.wait(timeout=5)
            for j in range(10):
                backend.execute(_INSERT, (f"n-{i}-{j}", "service"))
                backend.query(_SELECT)
        except BaseException as exc:  # noqa: BLE001
            errors.append(exc)

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=30)

    assert not errors, errors
    assert conn.max_in_flight == 1
    rows = backend.query("SELECT COUNT(*) AS c FROM registry_meta")
    assert rows[0]["c"] == 40


def test_connect_sets_a_busy_timeout(tmp_path):
    backend = connect({"kind": "sqlite", "path": str(tmp_path / "t.db")})
    assert backend.conn.execute("PRAGMA busy_timeout").fetchone()[0] >= 30000


def test_memory_backend_is_reusable_from_threads():
    backend = connect({"kind": "memory"})
    init_schema(backend.conn)
    backend.execute(_INSERT, ("a", "service"))
    out: list = []

    t = threading.Thread(target=lambda: out.append(backend.query(_SELECT)))
    t.start()
    t.join()

    assert out and out[0][0]["registry"] == "a"
