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
    """Daily-rotating, gzip-compressed log handler with date-stamped names.

    Current day:   a2x-registry-2026-08-11.log         (no .gz)
    Previous days: a2x-registry-2026-08-11.log.gz      (compressed)
    At each midnight rollover the finished day's file is gzipped, a fresh
    file stamped with the new date is opened, and the oldest ``.gz`` files
    beyond ``retention_days`` are pruned.
    """

    def __init__(self, log_dir: Path, stem: str, retention_days: int) -> None:
        self._log_dir = Path(log_dir)
        self._stem = stem
        self._retention_days = retention_days
        super().__init__(
            self._log_dir / self._dated_name(date.today()),
            when="midnight",
            interval=1,
            backupCount=retention_days,
            encoding="utf-8",
            delay=True,
        )

    def _dated_name(self, day: date) -> str:
        return f"{self._stem}-{day:%Y-%m-%d}.log"

    def doRollover(self) -> None:
        if self.stream:
            self.stream.close()
            self.stream = None
        cur = Path(self.baseFilename)
        try:
            if cur.exists():
                gz_path = Path(str(cur) + ".gz")
                with open(cur, "rb") as f_in, gzip.open(gz_path, "wb") as f_out:
                    shutil.copyfileobj(f_in, f_out)
                cur.unlink()
        except OSError as exc:
            # 压缩/删除失败时保留原日志文件（不删除，避免数据丢失），只记录问题。
            logger.exception("Failed to gzip rotated log %s: %s", cur, exc)
        finally:
            # 无论压缩是否成功，都切换到新日期文件并恢复可写流
            self.baseFilename = str(self._log_dir / self._dated_name(date.today()))
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