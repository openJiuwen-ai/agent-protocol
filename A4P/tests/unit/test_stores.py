from __future__ import annotations

import json
import sqlite3
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
from dataclasses import replace

import pytest

from a4p.credential_store import (
    CredentialStoreFormatError,
    JsonFileCredentialStore,
    UserCredentialRecord,
)
from a4p.intent.usage_store import SQLiteIntentTokenUsageStore


def test_json_file_credential_store_persists_records(tmp_path) -> None:
    path = tmp_path / "credentials.json"
    store = JsonFileCredentialStore(path)
    record = UserCredentialRecord(
        userId="demo-user",
        credentialId="cred-1",
        signatureMethod="webauthn",
        publicKey={"format": "cose", "value": "public-key"},
        details={
            "signCount": 1,
            "rpId": "localhost",
            "origin": "http://localhost:8970",
        },
    )

    store.save(record)
    store.save(replace(record, details={**record.details, "signCount": 2}))

    loaded = JsonFileCredentialStore(path).get("cred-1")
    assert loaded is not None
    assert loaded.details["signCount"] == 2
    assert loaded.userId == "demo-user"
    payload = json.loads(path.read_text(encoding="utf-8"))
    assert payload["schemaVersion"] == 2


def test_json_file_credential_store_refreshes_cross_process_records(tmp_path) -> None:
    path = tmp_path / "credentials.json"
    server_store = JsonFileCredentialStore(path)
    authorizer_store = JsonFileCredentialStore(path)
    record = UserCredentialRecord(
        userId="demo-user",
        credentialId="cred-1",
        signatureMethod="webauthn",
        publicKey={"format": "cose", "value": "public-key"},
        details={"signCount": 1},
    )

    authorizer_store.save(record)

    loaded = server_store.get("cred-1")
    assert loaded is not None
    assert loaded.userId == "demo-user"
    assert [item.credentialId for item in server_store.list_for_user("demo-user")] == [
        "cred-1"
    ]


@pytest.mark.parametrize(
    "legacy_payload",
    [
        [],
        {"credentials": []},
        [{"userId": "demo-user", "credentialId": "legacy"}],
    ],
)
def test_json_file_credential_store_rejects_legacy_format(
    tmp_path,
    legacy_payload,
) -> None:
    path = tmp_path / "credentials.json"
    path.write_text(json.dumps(legacy_payload), encoding="utf-8")

    with pytest.raises(
        CredentialStoreFormatError,
        match="delete the old credential file and register credentials again",
    ):
        JsonFileCredentialStore(path)


def test_sqlite_usage_store_consumes_atomically_across_connections(tmp_path) -> None:
    path = tmp_path / "usage.sqlite3"
    stores = [SQLiteIntentTokenUsageStore(path), SQLiteIntentTokenUsageStore(path)]
    expire_at_epoch = int(time.time()) + 60

    def consume(store: SQLiteIntentTokenUsageStore) -> tuple[bool, int]:
        return store.consume(
            token_id="token-1",
            max_executions=1,
            expire_at_epoch=expire_at_epoch,
        )

    with ThreadPoolExecutor(max_workers=2) as executor:
        results = list(executor.map(consume, stores))

    assert sorted(consumed for consumed, _used in results) == [False, True]
    assert [used for _consumed, used in results] == [1, 1]


def test_sqlite_usage_store_removes_expired_records(tmp_path) -> None:
    path = tmp_path / "usage.sqlite3"
    store = SQLiteIntentTokenUsageStore(path)
    future = int(time.time()) + 60
    assert store.consume(
        token_id="expired-token", max_executions=1, expire_at_epoch=future
    ) == (True, 1)

    with closing(sqlite3.connect(path)) as connection:
        connection.execute(
            "UPDATE intent_token_usage SET expire_at_epoch = 1 WHERE token_id = ?",
            ("expired-token",),
        )
        connection.commit()

    assert store.consume(
        token_id="active-token", max_executions=1, expire_at_epoch=future
    ) == (True, 1)
    with closing(sqlite3.connect(path)) as connection:
        row = connection.execute(
            "SELECT token_id FROM intent_token_usage WHERE token_id = ?",
            ("expired-token",),
        ).fetchone()
    assert row is None
