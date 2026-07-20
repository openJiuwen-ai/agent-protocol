"""In-place terminal progress bar for CLI taxonomy building."""

import logging
import sys

_logger = logging.getLogger(__name__)
_logger.setLevel(logging.INFO)  # ensure INFO records propagate regardless of root logger config


def progress_bar(done: int, total: int, *, width: int = 30, prefix: str = "", suffix: str = ""):
    """Write an in-place progress bar to stdout and emit a logging record.

    Call with done == total to finalize (appends newline).
    """
    if total <= 0:
        return
    filled = int(width * done / total)
    try:
        "█░".encode(sys.stdout.encoding or "utf-8")
        bar = "█" * filled + "░" * (width - filled)
    except (UnicodeEncodeError, LookupError):
        bar = "#" * filled + "-" * (width - filled)
    pct = done / total * 100
    if prefix:
        line = f"\r  {prefix} {bar} {pct:5.1f}% [{done}/{total}]"
    else:
        line = f"\r  {bar} {pct:5.1f}% [{done}/{total}]"
    if suffix:
        line += f" {suffix}"
    line += " " * 10  # clear trailing chars
    sys.stdout.write(line)
    sys.stdout.flush()
    if done >= total:
        sys.stdout.write("\n")
        sys.stdout.flush()
    # Emit to logging so web UI log capture receives progress updates
    log_msg = f"{'  ' + prefix if prefix else ''} {bar} {pct:5.1f}% [{done}/{total}]"
    if suffix:
        log_msg += f" {suffix}"
    _logger.info(log_msg.strip())
