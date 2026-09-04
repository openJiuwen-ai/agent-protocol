"""Carrier-neutral user credential storage."""

from __future__ import annotations

import json
import threading
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Protocol


CREDENTIAL_STORE_SCHEMA_VERSION = 2


class CredentialStoreFormatError(ValueError):
    """Raised when a persisted credential file uses an unsupported schema."""


@dataclass(frozen=True)
class UserCredentialRecord:
    userId: str
    credentialId: str
    signatureMethod: str
    publicKey: dict[str, Any]
    details: dict[str, Any] = field(default_factory=dict)
    metadata: dict[str, Any] = field(default_factory=dict)
    createdAt: str = ""


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
        super().__init__(self._load_records())

    def save(self, record: UserCredentialRecord) -> None:
        super().save(record)
        self._flush()

    def get(self, credential_id: str) -> UserCredentialRecord | None:
        self._refresh()
        return super().get(credential_id)

    def list_for_user(self, user_id: str) -> list[UserCredentialRecord]:
        self._refresh()
        return super().list_for_user(user_id)

    def list_all(self) -> list[UserCredentialRecord]:
        self._refresh()
        return super().list_all()

    def _load_records(self) -> list[UserCredentialRecord]:
        if not self.path.exists():
            return []
        raw = json.loads(self.path.read_text(encoding="utf-8"))
        if not isinstance(raw, dict) or raw.get("schemaVersion") != CREDENTIAL_STORE_SCHEMA_VERSION:
            raise CredentialStoreFormatError(
                "Unsupported credential store format; delete the old credential file "
                "and register credentials again"
            )
        items = raw.get("credentials")
        if not isinstance(items, list):
            raise CredentialStoreFormatError("Credential store 'credentials' must be a list")
        records = []
        for item in items:
            if not isinstance(item, dict):
                raise CredentialStoreFormatError("Credential store records must be objects")
            records.append(UserCredentialRecord(**item))
        return records

    def _refresh(self) -> None:
        records = self._load_records()
        with self._lock:
            self._records = {record.credentialId: record for record in records}

    def _flush(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        records = [asdict(record) for record in self._records.values()]
        self.path.write_text(
            json.dumps(
                {
                    "schemaVersion": CREDENTIAL_STORE_SCHEMA_VERSION,
                    "credentials": records,
                },
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            ),
            encoding="utf-8",
        )


__all__ = [
    "A4PCredentialStore",
    "CREDENTIAL_STORE_SCHEMA_VERSION",
    "CredentialStoreFormatError",
    "InMemoryCredentialStore",
    "JsonFileCredentialStore",
    "UserCredentialRecord",
    "utc_now_iso",
]
