"""Log files must stay owner-readable/writable only.

The registry is commonly started as root (systemd). Default ``open()``
honours the process umask, so root-created logs end up 0644 and any local
user can read them. Both the current-day log and gzipped archives must be
0600 regardless of umask.
"""

import gzip
import logging
import os
import stat
import time
from datetime import date
from pathlib import Path

import pytest

from a2x_registry.common.log import DailyCompressedFileHandler

pytestmark = pytest.mark.skipif(
    os.name != "posix", reason="POSIX permission bits"
)


def _mode(path: Path) -> int:
    return stat.S_IMODE(path.stat().st_mode)


def _emit(handler: DailyCompressedFileHandler, msg: str = "hello") -> None:
    handler.setFormatter(logging.Formatter("%(message)s"))
    handler.emit(
        logging.LogRecord("test", logging.INFO, __file__, 1, msg, None, None)
    )


def test_current_log_is_owner_only(tmp_path):
    handler = DailyCompressedFileHandler(tmp_path, "a2x-registry", 7)
    try:
        _emit(handler)
        log = tmp_path / "a2x-registry.log"
        assert log.exists()
        assert _mode(log) == 0o600
    finally:
        handler.close()


def test_existing_world_readable_log_is_tightened(tmp_path):
    log = tmp_path / "a2x-registry.log"
    log.write_text("")
    log.chmod(0o644)

    handler = DailyCompressedFileHandler(tmp_path, "a2x-registry", 7)
    try:
        assert _mode(log) == 0o600
    finally:
        handler.close()


def test_archived_gz_is_owner_only(tmp_path):
    log = tmp_path / "a2x-registry.log"
    log.write_text("previous day\n")
    old = time.time() - 2 * 86400
    os.utime(log, (old, old))

    handler = DailyCompressedFileHandler(tmp_path, "a2x-registry", 7)
    try:
        archives = list(tmp_path.glob("a2x-registry-*.log.gz"))
        assert len(archives) == 1
        assert _mode(archives[0]) == 0o600
    finally:
        handler.close()


def test_existing_world_readable_archive_is_tightened(tmp_path):
    log = tmp_path / "a2x-registry.log"
    log.write_text("previous day\n")
    old = time.time() - 2 * 86400
    day = date.fromtimestamp(old)
    os.utime(log, (old, old))

    archive = tmp_path / f"a2x-registry-{day:%Y-%m-%d}.log.gz"
    archive.write_bytes(b"stale archive")
    archive.chmod(0o644)

    handler = DailyCompressedFileHandler(tmp_path, "a2x-registry", 7)
    try:
        assert _mode(archive) == 0o600
        with gzip.open(archive, "rb") as f:
            assert f.read() == b"previous day\n"
    finally:
        handler.close()


def test_archive_failure_preserves_existing_archive(tmp_path):
    handler = DailyCompressedFileHandler(tmp_path, "a2x-registry", 7)
    day = date.today()
    archive = tmp_path / f"a2x-registry-{day:%Y-%m-%d}.log.gz"
    archive.write_bytes(b"keep me")
    try:
        with pytest.raises(FileNotFoundError):
            handler._archive(tmp_path / "missing.log", day)
        assert archive.read_bytes() == b"keep me"
    finally:
        handler.close()


def test_chmod_failure_does_not_break_logging(tmp_path, monkeypatch):
    def _deny(*args, **kwargs):
        raise PermissionError("not owner")

    monkeypatch.setattr(os, "chmod", _deny)

    log = tmp_path / "a2x-registry.log"
    log.write_text("")
    handler = DailyCompressedFileHandler(tmp_path, "a2x-registry", 7)
    try:
        _emit(handler, "still logged")
        assert "still logged" in log.read_text()
    finally:
        handler.close()
