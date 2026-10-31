"""Cross-platform atomic JSON file write.

Unique sibling temp-file + ``os.replace`` (atomic on POSIX and Windows),
with one retry if replacement is briefly denied (e.g. by an AV scanner).
Failed writes leave the existing destination intact and propagate the error.

Shared helper so modules that persist small JSON state (cluster identity,
etc.) don't each re-implement the dance.
"""

from __future__ import annotations

import json
import os
import tempfile
import time
from pathlib import Path
from typing import Any


def atomic_write_json(path: Path, data: Any) -> None:
    """Write ``data`` as pretty UTF-8 JSON to ``path`` atomically.

    The parent directory must exist. Trailing newline is added for clean
    diffs. Flush the temporary file before replacing the destination. If
    replacement is denied, retry once after a short sleep, then propagate
    the error. Concurrent writers use separate files; the last successful
    replacement wins. Temporary-file cleanup is best effort on failure.
    """
    path = Path(path)
    content = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    tmp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=f".{path.name}.", suffix=".tmp", delete=False,
        ) as f:
            tmp_path = Path(f.name)
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
        try:
            os.replace(str(tmp_path), str(path))
        except PermissionError:
            time.sleep(0.05)
            os.replace(str(tmp_path), str(path))
    finally:
        if tmp_path is not None:
            try:
                tmp_path.unlink(missing_ok=True)
            except OSError:
                # Cleanup must not hide the original write/replace failure.
                pass
