"""Carrier-neutral user credential storage."""

from __future__ import annotations

import contextlib
import json
import logging
import os
import threading
import time
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path
from tempfile import NamedTemporaryFile
from collections.abc import Iterator
from typing import Any, Protocol


CREDENTIAL_STORE_SCHEMA_VERSION = 2
_logger = logging.getLogger(__name__)

# Credentials live in one JSON file that every process rewrites wholesale, so a
# per-instance threading.Lock is not enough: two processes (for example the
# server and a separately started user authorizer) read the same snapshot and
# the slower one silently replaces the other's record. Every read-modify-write
# cycle is therefore serialized by an OS-level lock on a sidecar file.
LOCK_RETRY_INTERVAL_SECONDS = 0.01
LOCK_TIMEOUT_SECONDS = 10.0
REPLACE_RETRY_ATTEMPTS = 5


class CredentialStoreFormatError(ValueError):
    """Raised when a persisted credential file has an invalid or unsupported format."""


class CredentialStoreLockError(RuntimeError):
    """Raised when the cross-process credential store lock cannot be acquired."""


@contextlib.contextmanager
def _exclusive_file_lock(lock_path: Path) -> Iterator[None]:
    """Hold an exclusive inter-process lock for one credential store file."""
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(lock_path, "a+b")
    try:
        if os.name == "nt":
            import msvcrt

            deadline = time.monotonic() + LOCK_TIMEOUT_SECONDS
            while True:
                try:
                    handle.seek(0)
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise CredentialStoreLockError(
                            f"Timed out waiting for credential store lock {lock_path}"
                        ) from None
                    time.sleep(LOCK_RETRY_INTERVAL_SECONDS)
        else:
            import fcntl

            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            if os.name == "nt":
                import msvcrt

                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
    finally:
        handle.close()


@dataclass(frozen=True)
class UserCredentialRecord:
    userId: str
    credentialId: str
    signatureMethod: str
    publicKey: dict[str, Any]
    details: dict[str, Any] = field(default_factory=dict)
    metadata: dict[str, Any] = field(default_factory=dict)
    createdAt: str = ""


_CREDENTIAL_RECORD_FIELDS = {item.name for item in fields(UserCredentialRecord)}


def _valid_record(record: UserCredentialRecord) -> bool:
    return (
        all(
            isinstance(value, str) and value.strip()
            for value in (record.userId, record.credentialId, record.signatureMethod)
        )
        and isinstance(record.publicKey, dict)
        and isinstance(record.details, dict)
        and isinstance(record.metadata, dict)
        and isinstance(record.createdAt, str)
    )


class A4PCredentialStore(Protocol):
    def save(self, record: UserCredentialRecord) -> None:
        """Persist or replace a user credential record."""

    def get(self, credential_id: str) -> UserCredentialRecord | None:
        """Return one credential record by credential id."""

    def list_for_user(self, user_id: str) -> list[UserCredentialRecord]:
        """Return all credential records registered for a user."""

    def list_all(self) -> list[UserCredentialRecord]:
        """Return all credential records."""

def utc_now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


class InMemoryCredentialStore:
    def __init__(self, records: list[UserCredentialRecord] | None = None) -> None:
        self._records: dict[str, UserCredentialRecord] = {}
        self._lock = threading.Lock()
        for record in records or []:
            self.save(record)

    def save(self, record: UserCredentialRecord) -> None:
        with self._lock:
            self._records[record.credentialId] = record

    def get(self, credential_id: str) -> UserCredentialRecord | None:
        with self._lock:
            return self._records.get(credential_id)

    def list_for_user(self, user_id: str) -> list[UserCredentialRecord]:
        with self._lock:
            return [record for record in self._records.values() if record.userId == user_id]

    def list_all(self) -> list[UserCredentialRecord]:
        with self._lock:
            return list(self._records.values())

class JsonFileCredentialStore(InMemoryCredentialStore):
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        super().__init__()
        self._refresh()

    def save(self, record: UserCredentialRecord) -> None:
        if not _valid_record(record):
            raise ValueError("Invalid credential record")
        with self._lock, _exclusive_file_lock(self._lock_path()):
            try:
                payload = self._read_payload()
            except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                raise CredentialStoreFormatError(
                    f"Invalid JSON in credential store {self.path}; restore the file from a backup"
                ) from exc
            items = payload["credentials"]
            serialized_record = asdict(record)
            for index in range(len(items) - 1, -1, -1):
                item = items[index]
                if isinstance(item, dict) and item.get("credentialId") == record.credentialId:
                    items[index] = {**item, **serialized_record}
                    break
            else:
                items.append(serialized_record)
            self._flush(payload)

    def _lock_path(self) -> Path:
        return self.path.with_name(self.path.name + ".lock")

    def get(self, credential_id: str) -> UserCredentialRecord | None:
        self._refresh()
        return super().get(credential_id)

    def list_for_user(self, user_id: str) -> list[UserCredentialRecord]:
        self._refresh()
        return super().list_for_user(user_id)

    def list_all(self) -> list[UserCredentialRecord]:
        self._refresh()
        return super().list_all()

    def _read_payload(self) -> dict[str, Any]:
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {"schemaVersion": CREDENTIAL_STORE_SCHEMA_VERSION, "credentials": []}
        if not isinstance(raw, dict) or raw.get("schemaVersion") != CREDENTIAL_STORE_SCHEMA_VERSION:
            raise CredentialStoreFormatError(
                "Unsupported credential store format; delete the old credential file "
                "and register credentials again"
            )
        if not isinstance(raw.get("credentials"), list):
            raise CredentialStoreFormatError("Credential store 'credentials' must be a list")
        return raw

    def _load_records(self) -> dict[str, UserCredentialRecord]:
        try:
            payload = self._read_payload()
        except (json.JSONDecodeError, UnicodeDecodeError):
            _logger.error("Invalid JSON in credential store %s; no credentials loaded", self.path)
            return {}
        records = {}
        for index, item in enumerate(payload["credentials"]):
            if isinstance(item, dict):
                try:
                    record = UserCredentialRecord(**{
                        key: value for key, value in item.items()
                        if key in _CREDENTIAL_RECORD_FIELDS
                    })
                except TypeError:
                    pass
                else:
                    if _valid_record(record):
                        records[record.credentialId] = record
                        continue
            _logger.warning("Skipping invalid credential record at index %d in %s", index, self.path)
        return records

    def _refresh(self) -> None:
        with self._lock:
            self._records = self._load_records()

    def _flush(self, payload: dict[str, Any]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = None
        try:
            with NamedTemporaryFile(
                mode="w", encoding="utf-8", dir=self.path.parent, delete=False,
            ) as temporary_file:
                temporary_path = Path(temporary_file.name)
                json.dump(
                    payload,
                    temporary_file,
                    ensure_ascii=False,
                    indent=2,
                    sort_keys=True,
                )
                temporary_file.flush()
                os.fsync(temporary_file.fileno())
            for attempt in range(REPLACE_RETRY_ATTEMPTS):
                try:
                    os.replace(temporary_path, self.path)
                except PermissionError:
                    # Windows reports a sharing violation while another process
                    # still holds the target open; retry briefly instead of
                    # failing an otherwise valid write.
                    if attempt + 1 >= REPLACE_RETRY_ATTEMPTS:
                        raise
                    time.sleep(LOCK_RETRY_INTERVAL_SECONDS * (attempt + 1))
                else:
                    break
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)


__all__ = [
    "A4PCredentialStore",
    "CredentialStoreLockError",
    "CREDENTIAL_STORE_SCHEMA_VERSION",
    "CredentialStoreFormatError",
    "InMemoryCredentialStore",
    "JsonFileCredentialStore",
    "UserCredentialRecord",
    "utc_now_iso",
]
