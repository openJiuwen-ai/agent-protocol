from __future__ import annotations

import json
import multiprocessing
import sqlite3
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
from dataclasses import asdict, replace

import pytest

from a4p import credential_store
from a4p.credential_store import (
    CREDENTIAL_STORE_SCHEMA_VERSION,
    CredentialStoreFormatError,
    JsonFileCredentialStore,
    UserCredentialRecord,
)
from a4p.intent.usage_store import SQLiteIntentTokenUsageStore


def test_json_file_credential_store_persists_records(tmp_path) -> None:
    """JSON 凭据存储保存记录并重新打开后，应完整恢复凭据内容。"""
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
    """多个 JSON 凭据存储实例共享文件时，一个实例写入的记录应被另一个实例刷新读取。"""
    path = tmp_path / "credentials.json"
    server_store = JsonFileCredentialStore(path)
    authorizer_store = JsonFileCredentialStore(path)
    other_store = JsonFileCredentialStore(path)
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
    other_store.save(replace(record, credentialId="cred-2"))
    assert {item.credentialId for item in authorizer_store.list_all()} == {
        "cred-1", "cred-2",
    }


@pytest.mark.parametrize("damaged", [b'{"schemaVersion": 2,', b"\xff"])
def test_json_file_credential_store_preserves_unreadable_file(
    tmp_path, caplog, damaged: bytes,
) -> None:
    path = tmp_path / "credentials.json"
    record = UserCredentialRecord("demo-user", "cred-1", "ed25519", {})
    original_store = JsonFileCredentialStore(path)
    original_store.save(record)
    backup = path.read_bytes()
    path.write_bytes(damaged)

    store = JsonFileCredentialStore(path)
    assert store.list_all() == []
    assert original_store.get("cred-1") is None
    assert "Invalid JSON in credential store" in caplog.text
    with pytest.raises(CredentialStoreFormatError, match="Invalid JSON"):
        store.save(record)
    assert path.read_bytes() == damaged

    path.write_bytes(backup)
    assert store.get("cred-1") == record


def test_json_file_credential_store_isolates_invalid_records(tmp_path, caplog) -> None:
    path = tmp_path / "credentials.json"
    valid = UserCredentialRecord("demo-user", "cred-1", "ed25519", {"x": "key"})
    path.write_text(
        json.dumps({
            "schemaVersion": CREDENTIAL_STORE_SCHEMA_VERSION,
            "credentials": [
                {**asdict(valid), "extension": "unrecognized"},
                {**asdict(valid), "credentialId": "bad-key", "publicKey": None},
                {**asdict(valid), "credentialId": "bad-details", "details": None},
                {"userId": "demo-user"},
                None,
            ],
        }),
        encoding="utf-8",
    )

    store = JsonFileCredentialStore(path)
    assert store.list_all() == [valid]
    assert "Skipping invalid credential record" in caplog.text

    updated = replace(valid, metadata={"label": "primary"})
    store.save(updated)
    assert store.list_all() == [updated]
    payload = json.loads(path.read_bytes())
    assert payload["credentials"][0]["extension"] == "unrecognized"
    assert payload["credentials"][1]["publicKey"] is None


def test_json_file_credential_store_keeps_previous_file_when_replace_fails(
    tmp_path, monkeypatch,
) -> None:
    path = tmp_path / "credentials.json"
    store = JsonFileCredentialStore(path)
    record = UserCredentialRecord("demo-user", "cred-1", "ed25519", {})
    store.save(record)
    previous = path.read_bytes()

    def fail_replace(source, destination) -> None:
        assert destination == path
        assert source.parent == path.parent
        assert path.read_bytes() == previous
        assert json.loads(source.read_bytes())["credentials"][0]["details"] == {"updated": True}
        raise OSError("replace interrupted")

    monkeypatch.setattr(credential_store.os, "replace", fail_replace)
    with pytest.raises(OSError, match="replace interrupted"):
        store.save(replace(record, details={"updated": True}))
    assert path.read_bytes() == previous
    # The cross-process lock sidecar is the only allowed extra artifact; the
    # interrupted write must not leave its temporary file behind.
    assert sorted(item.name for item in tmp_path.iterdir()) == [
        "credentials.json",
        "credentials.json.lock",
    ]
    assert store.get("cred-1") == record


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
    """JSON 凭据存储读取旧版或非法顶层格式时，应明确拒绝而不是静默迁移。"""
    path = tmp_path / "credentials.json"
    path.write_text(json.dumps(legacy_payload), encoding="utf-8")

    with pytest.raises(
        CredentialStoreFormatError,
        match="delete the old credential file and register credentials again",
    ):
        JsonFileCredentialStore(path)


def test_sqlite_usage_store_consumes_atomically_across_connections(tmp_path) -> None:
    """多个 SQLite 用量存储连接并发消费同一 token 配额时，应原子计数且不超过上限。"""
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
    """SQLite 用量存储清理过期数据时，应删除过期记录并保留仍有效记录。"""
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


def _save_many_records(store_path: str, tag: str, count: int) -> None:
    store = JsonFileCredentialStore(store_path)
    for index in range(count):
        store.save(UserCredentialRecord("demo-user", f"cred-{tag}-{index}", "ed25519", {}))


def test_json_file_credential_store_keeps_concurrent_cross_process_writes(tmp_path) -> None:
    """两个进程并发写同一凭据文件时，两份记录都必须保留。

    没有跨进程互斥时 save() 是读-改-写全量覆盖：两个进程读到同一快照，
    后写者会把先写者的记录整条抹掉（实测 4 进程 x 25 条只剩 26~33 条）。
    """
    path = tmp_path / "credentials.json"
    JsonFileCredentialStore(path).save(
        UserCredentialRecord("demo-user", "seed", "ed25519", {})
    )

    methods = multiprocessing.get_all_start_methods()
    context = multiprocessing.get_context("fork" if "fork" in methods else "spawn")
    processes = [
        context.Process(target=_save_many_records, args=(str(path), tag, 40))
        for tag in ("alpha", "beta")
    ]
    for process in processes:
        process.start()
    for process in processes:
        process.join(timeout=180)
        assert process.exitcode == 0

    credential_ids = {
        record.credentialId for record in JsonFileCredentialStore(path).list_all()
    }
    expected = {"seed"} | {
        f"cred-{tag}-{index}" for tag in ("alpha", "beta") for index in range(40)
    }
    assert credential_ids == expected
