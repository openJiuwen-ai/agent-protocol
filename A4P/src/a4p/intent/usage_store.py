"""Persistent execution-usage storage for A4P intent tokens."""

from __future__ import annotations

import os
import sqlite3
import time
from pathlib import Path
from typing import Protocol


DEFAULT_INTENT_TOKEN_USAGE_DB_PATH = ".a4p/intent_token_usage.sqlite3"


class IntentTokenUsageStoreError(RuntimeError):
    """Raised when token usage cannot be consumed safely."""


class A4PIntentTokenUsageStore(Protocol):
    def consume(
        self,
        *,
        token_id: str,
        max_executions: int,
        expire_at_epoch: int,
    ) -> tuple[bool, int]:
        """Atomically consume one execution and return (consumed, executions_used)."""


def default_intent_token_usage_db_path() -> str:
    """Return the configured SQLite path for intent-token usage state."""
    return (os.getenv("A4P_USAGE_DB_PATH") or DEFAULT_INTENT_TOKEN_USAGE_DB_PATH).strip()


class SQLiteIntentTokenUsageStore:
    """SQLite-backed token usage store with atomic cross-process consumption."""

    def __init__(self, path: str | Path | None = None, *, timeout_seconds: float = 5.0) -> None:
        configured_path = str(path) if path is not None else default_intent_token_usage_db_path()
        if not configured_path.strip():
            raise ValueError("Intent token usage database path must not be empty")
        if timeout_seconds <= 0:
            raise ValueError("Intent token usage database timeout must be positive")
        self.path = Path(configured_path.strip())
        self.timeout_seconds = float(timeout_seconds)

    def consume(
        self,
        *,
        token_id: str,
        max_executions: int,
        expire_at_epoch: int,
    ) -> tuple[bool, int]:
        normalized_token_id = token_id.strip()
        if not normalized_token_id:
            raise ValueError("token_id must not be empty")
        if isinstance(max_executions, bool) or not isinstance(max_executions, int) or max_executions <= 0:
            raise ValueError("max_executions must be a positive integer")
        if isinstance(expire_at_epoch, bool) or not isinstance(expire_at_epoch, int) or expire_at_epoch <= 0:
            raise ValueError("expire_at_epoch must be a positive integer")

        connection: sqlite3.Connection | None = None
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            connection = sqlite3.connect(
                self.path,
                timeout=self.timeout_seconds,
                isolation_level=None,
            )
            connection.execute(f"PRAGMA busy_timeout = {int(self.timeout_seconds * 1000)}")
            connection.execute("BEGIN IMMEDIATE")
            connection.execute(
                """
                CREATE TABLE IF NOT EXISTS intent_token_usage (
                    token_id TEXT PRIMARY KEY,
                    executions_used INTEGER NOT NULL,
                    executions_limit INTEGER NOT NULL,
                    expire_at_epoch INTEGER NOT NULL
                )
                """
            )
            connection.execute(
                "DELETE FROM intent_token_usage WHERE expire_at_epoch <= ?",
                (int(time.time()),),
            )
            row = connection.execute(
                """
                SELECT executions_used, executions_limit, expire_at_epoch
                FROM intent_token_usage
                WHERE token_id = ?
                """,
                (normalized_token_id,),
            ).fetchone()

            if row is None:
                executions_used = 1
                connection.execute(
                    """
                    INSERT INTO intent_token_usage (
                        token_id,
                        executions_used,
                        executions_limit,
                        expire_at_epoch
                    ) VALUES (?, ?, ?, ?)
                    """,
                    (
                        normalized_token_id,
                        executions_used,
                        max_executions,
                        expire_at_epoch,
                    ),
                )
                connection.commit()
                return True, executions_used

            executions_used, stored_limit, stored_expire_at = (int(value) for value in row)
            if stored_limit != max_executions or stored_expire_at != expire_at_epoch:
                raise IntentTokenUsageStoreError(
                    "Stored intent token usage policy does not match the signed token"
                )
            if executions_used >= max_executions:
                connection.commit()
                return False, executions_used

            executions_used += 1
            connection.execute(
                "UPDATE intent_token_usage SET executions_used = ? WHERE token_id = ?",
                (executions_used, normalized_token_id),
            )
            connection.commit()
            return True, executions_used
        except IntentTokenUsageStoreError:
            if connection is not None and connection.in_transaction:
                connection.rollback()
            raise
        except (OSError, sqlite3.Error) as exc:
            if connection is not None and connection.in_transaction:
                connection.rollback()
            raise IntentTokenUsageStoreError("Intent token usage store unavailable") from exc
        finally:
            if connection is not None:
                connection.close()


__all__ = [
    "A4PIntentTokenUsageStore",
    "DEFAULT_INTENT_TOKEN_USAGE_DB_PATH",
    "IntentTokenUsageStoreError",
    "SQLiteIntentTokenUsageStore",
    "default_intent_token_usage_db_path",
]
