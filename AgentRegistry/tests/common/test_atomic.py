"""Atomic state writes preserve the destination on failure and isolate writers."""

import errno
import json
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from threading import Barrier, local

import pytest

from a2x_registry.common import atomic


def test_write_replaces_complete_json_and_preserves_unrelated_sidecar(tmp_path):
    path = tmp_path / "state.json"
    path.write_text('{"old": true}', encoding="utf-8")
    sidecar = tmp_path / "state.json.tmp"
    sidecar.write_text("unrelated file", encoding="utf-8")

    atomic.atomic_write_json(path, {"node_id": "节点", "version": 2})

    assert json.loads(path.read_text(encoding="utf-8")) == {"node_id": "节点", "version": 2}
    assert path.read_text(encoding="utf-8").endswith("\n")
    assert sidecar.read_text(encoding="utf-8") == "unrelated file"
    assert set(tmp_path.iterdir()) == {path, sidecar}


@pytest.mark.parametrize("existing", [False, True])
def test_denied_replacement_propagates_without_overwriting(tmp_path, monkeypatch, existing):
    path = tmp_path / "state.json"
    original = b'{"old": true}\n'
    if existing:
        path.write_bytes(original)
    attempts = []

    def deny_replace(source, target):
        attempts.append(source)
        raise PermissionError("destination locked")

    monkeypatch.setattr(atomic.os, "replace", deny_replace)
    monkeypatch.setattr(atomic.time, "sleep", lambda _: None)
    with pytest.raises(PermissionError, match="destination locked"):
        atomic.atomic_write_json(path, {"new": True})

    assert len(attempts) == 2
    assert attempts[0] == attempts[1]
    if existing:
        assert path.read_bytes() == original
    else:
        assert not path.exists()
    assert set(tmp_path.iterdir()) == ({path} if existing else set())


def test_transient_permission_failure_retries_complete_file(tmp_path, monkeypatch):
    path = tmp_path / "state.json"
    path.write_text('{"old": true}', encoding="utf-8")
    real_replace = os.replace
    attempts = []

    def replace(source, target):
        attempts.append(source)
        assert json.loads(Path(source).read_text(encoding="utf-8")) == {"new": True}
        if len(attempts) == 1:
            raise PermissionError("temporarily locked")
        assert json.loads(path.read_text(encoding="utf-8")) == {"old": True}
        real_replace(source, target)

    monkeypatch.setattr(atomic.os, "replace", replace)
    monkeypatch.setattr(atomic.time, "sleep", lambda _: None)
    atomic.atomic_write_json(path, {"new": True})

    assert len(attempts) == 2
    assert json.loads(path.read_text(encoding="utf-8")) == {"new": True}
    assert set(tmp_path.iterdir()) == {path}


def test_storage_error_preserves_old_file_without_retry(tmp_path, monkeypatch):
    path = tmp_path / "state.json"
    path.write_bytes(b"original state")
    attempts = []

    def no_space(source, target):
        attempts.append(source)
        raise OSError(errno.ENOSPC, "no space")

    monkeypatch.setattr(atomic.os, "replace", no_space)
    with pytest.raises(OSError) as error:
        atomic.atomic_write_json(path, {"new": True})
    assert error.value.errno == errno.ENOSPC
    assert len(attempts) == 1
    assert path.read_bytes() == b"original state"
    assert set(tmp_path.iterdir()) == {path}


def test_flush_failure_preserves_old_file_and_cleans_temp(tmp_path, monkeypatch):
    path = tmp_path / "state.json"
    path.write_bytes(b"original state")

    def fail_flush(_):
        raise OSError(errno.EIO, "flush failed")

    monkeypatch.setattr(atomic.os, "fsync", fail_flush)
    with pytest.raises(OSError, match="flush failed"):
        atomic.atomic_write_json(path, {"new": True})
    assert path.read_bytes() == b"original state"
    assert set(tmp_path.iterdir()) == {path}


def test_serialization_failure_leaves_no_partial_files(tmp_path):
    path = tmp_path / "state.json"
    path.write_bytes(b"original state")
    with pytest.raises(TypeError):
        atomic.atomic_write_json(path, object())
    assert path.read_bytes() == b"original state"
    assert set(tmp_path.iterdir()) == {path}


def test_concurrent_writers_use_separate_complete_sibling_files(tmp_path, monkeypatch):
    path = tmp_path / "state.json"
    real_replace = os.replace
    barrier = Barrier(2)
    writer_state = local()
    sources = []
    payloads = []

    def replace(source, target):
        source = Path(source)
        assert source.parent == path.parent
        # Synchronize each writer once; a Windows permission retry must not
        # wait for a second writer that has already completed its replacement.
        if not getattr(writer_state, "ready", False):
            writer_state.ready = True
            sources.append(source)
            barrier.wait(timeout=10)
            payloads.append(json.loads(source.read_text(encoding="utf-8")))
        real_replace(source, target)

    monkeypatch.setattr(atomic.os, "replace", replace)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda writer: atomic.atomic_write_json(path, {"writer": writer}), [1, 2]))

    assert len(set(sources)) == 2
    assert sorted(item["writer"] for item in payloads) == [1, 2]
    assert json.loads(path.read_text(encoding="utf-8"))["writer"] in (1, 2)
    assert set(tmp_path.iterdir()) == {path}
