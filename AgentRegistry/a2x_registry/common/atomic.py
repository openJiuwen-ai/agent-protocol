"""Cross-platform atomic JSON file write.

Temp-file + ``os.replace`` (atomic on POSIX and Windows), with a retry +
direct-overwrite fallback for the Windows case where ``os.replace`` can
transiently fail if the target is briefly locked (e.g. by an AV scanner).

Shared helper so modules that persist small JSON state (cluster identity,
etc.) don't each re-implement the dance.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


def atomic_write_json(path: Path, data: Any) -> None:
    """Write ``data`` as pretty UTF-8 JSON to ``path`` atomically.

    The parent directory must exist. Trailing newline is added for clean
    diffs. On the rare Windows ``os.replace`` failure, retries once after a
    short sleep, then falls back to a direct (non-atomic) overwrite.
    """
    path = Path(path)
    content = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    with open(tmp_path, "w", encoding="utf-8") as f:
        f.write(content)
    try:
        os.replace(str(tmp_path), str(path))
    except OSError:
        import time
        time.sleep(0.05)
        try:
            os.replace(str(tmp_path), str(path))
        except OSError:
            with open(path, "w", encoding="utf-8") as f:
                f.write(content)
            if tmp_path.exists():
                tmp_path.unlink(missing_ok=True)
