from __future__ import annotations

import time

from a4p.intent.usage_store import SQLiteIntentTokenUsageStore


def test_expired_token_is_never_consumed(tmp_path) -> None:
    """过期 token 不能消费成功，也不能因为过期行清理而被重置额度。

    清理过期行（DELETE ... WHERE expire_at_epoch <= now）之后 "row is None" 分支
    会重新 INSERT 成 executions_used = 1，因此缺少过期判断时过期 token 永远返回成功。
    """
    store = SQLiteIntentTokenUsageStore(tmp_path / "usage.sqlite3")
    expire_at_epoch = int(time.time()) - 60

    results = [
        store.consume(token_id="expired-token", max_executions=2, expire_at_epoch=expire_at_epoch)
        for _ in range(3)
    ]

    assert results == [(False, 0), (False, 0), (False, 0)]


def test_expired_token_does_not_leave_usage_state_behind(tmp_path) -> None:
    """过期 token 的拒绝不应在库里留下占用记录。"""
    path = tmp_path / "usage.sqlite3"
    store = SQLiteIntentTokenUsageStore(path)

    assert store.consume(
        token_id="expired-token", max_executions=2, expire_at_epoch=int(time.time()) - 60
    ) == (False, 0)

    import sqlite3

    with sqlite3.connect(path) as connection:
        rows = connection.execute(
            "SELECT token_id FROM intent_token_usage WHERE token_id = ?", ("expired-token",)
        ).fetchall()
    assert rows == []


def test_active_token_still_consumes_within_its_quota(tmp_path) -> None:
    """过期判断不能影响仍在有效期内的 token。"""
    store = SQLiteIntentTokenUsageStore(tmp_path / "usage.sqlite3")
    expire_at_epoch = int(time.time()) + 60

    assert store.consume(
        token_id="active-token", max_executions=2, expire_at_epoch=expire_at_epoch
    ) == (True, 1)
    assert store.consume(
        token_id="active-token", max_executions=2, expire_at_epoch=expire_at_epoch
    ) == (True, 2)
    assert store.consume(
        token_id="active-token", max_executions=2, expire_at_epoch=expire_at_epoch
    ) == (False, 2)
