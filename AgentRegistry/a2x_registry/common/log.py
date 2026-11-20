"""Daily-rotating, gzip-compressed file log handler for the registry backend."""

from __future__ import annotations

import gzip
import logging
import logging.handlers
import shutil
import time
from datetime import date
from pathlib import Path

logger = logging.getLogger(__name__)


class DailyCompressedFileHandler(logging.handlers.TimedRotatingFileHandler):
    """Daily-rotating, gzip-compressed log handler with a fixed current name.

    Current day:   a2x-registry.log                     (fixed name, no date)
    Previous days: a2x-registry-2026-08-11.log.gz       (date-stamped, compressed)
    At each midnight rollover the finished day's file is gzipped under the
    date it was written, a fresh fixed-name file is opened, and the oldest
    ``.gz`` files beyond ``retention_days`` are pruned. A fixed-name file
    left over from a previous day (service down across midnight) is
    archived on startup based on its mtime.
    """

    def __init__(self, log_dir: Path, stem: str, retention_days: int) -> None:
        self._log_dir = Path(log_dir)
        self._stem = stem
        self._retention_days = retention_days
        self._current_day = date.today()
        super().__init__(
            self._log_dir / self._fixed_name(),
            when="midnight",
            interval=1,
            backupCount=retention_days,
            encoding="utf-8",
            delay=True,
        )
        self._archive_stale_file()

    def _fixed_name(self) -> str:
        return f"{self._stem}.log"

    def _archived_name(self, day: date) -> str:
        return f"{self._stem}-{day:%Y-%m-%d}.log.gz"

    def _archive(self, src: Path, day: date) -> None:
        gz_path = self._log_dir / self._archived_name(day)
        with open(src, "rb") as f_in, gzip.open(gz_path, "wb") as f_out:
            shutil.copyfileobj(f_in, f_out)
        src.unlink()

    def _archive_stale_file(self) -> None:
        """启动时归档跨天残留的固定名日志（服务停跨天后重启的场景）。"""
        cur = self._log_dir / self._fixed_name()
        try:
            if cur.exists():
                mtime_day = date.fromtimestamp(cur.stat().st_mtime)
                if mtime_day != self._current_day:
                    self._archive(cur, mtime_day)
                    self._prune()
        except OSError as exc:
            # 归档失败时保留原日志文件（不删除，避免数据丢失），只记录问题。
            logger.exception("Failed to archive stale log %s: %s", cur, exc)

    def doRollover(self) -> None:
        if self.stream:
            self.stream.close()
            self.stream = None
        cur = Path(self.baseFilename)
        try:
            if cur.exists():
                self._archive(cur, self._current_day)
        except OSError as exc:
            # 压缩/删除失败时保留原日志文件（不删除，避免数据丢失），只记录问题。
            logger.exception("Failed to gzip rotated log %s: %s", cur, exc)
        finally:
            # 无论压缩是否成功，都重开固定名文件并恢复可写流
            self._current_day = date.today()
            self.baseFilename = str(self._log_dir / self._fixed_name())
            self.stream = self._open()
            self._prune()
            self.rolloverAt = self.computeRollover(time.time())

    def _prune(self) -> None:
        files = sorted(
            self._log_dir.glob(f"{self._stem}-*.log.gz"),
            key=lambda p: p.name,
        )
        for old in files[: max(0, len(files) - self._retention_days)]:
            try:
                old.unlink()
            except OSError:
                pass