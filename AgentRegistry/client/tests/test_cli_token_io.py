"""cli_token.json read/write/remove round-trip + permission handling."""

from __future__ import annotations

import json
import os
import stat

import pytest

from a2x_registry_client.auth import (
    read_cli_token,
    remove_cli_token,
    write_cli_token,
)


def test_write_then_read_roundtrip(isolated_home):
    path = write_cli_token("a2x_pat_token_xxxxx", "http://example/")
    assert path.exists()
    cfg = read_cli_token(path)
    assert cfg == {"base_url": "http://example/", "api_key": "a2x_pat_token_xxxxx"}


def test_write_creates_parent_dir(isolated_home):
    # Confirm the parent dir doesn't exist beforehand.
    cfg_path = isolated_home / ".a2x_registry_client" / "cli_token.json"
    assert not cfg_path.parent.exists()
    write_cli_token("a2x_pat_x" * 6, "http://x/")
    assert cfg_path.exists()


@pytest.mark.skipif(os.name != "posix", reason="POSIX-only chmod check")
def test_write_sets_0600_perms(isolated_home):
    path = write_cli_token("a2x_pat_x" * 6, "http://x/")
    mode = path.stat().st_mode & 0o777
    assert mode == 0o600, f"expected 0o600, got {oct(mode)}"


def test_read_missing_returns_none(isolated_home):
    assert read_cli_token() is None


def test_remove_is_idempotent(isolated_home):
    # First call on non-existing file → False, no exception.
    assert remove_cli_token() is False
    write_cli_token("a2x_pat_x" * 6, "http://x/")
    assert remove_cli_token() is True
    # Subsequent removes are no-ops.
    assert remove_cli_token() is False


def test_write_rejects_empty_api_key(isolated_home):
    with pytest.raises(ValueError):
        write_cli_token("", "http://x/")
    with pytest.raises(ValueError):
        write_cli_token("   ", "http://x/")


def test_read_corrupt_file_returns_none_with_warning(isolated_home, recwarn):
    cfg = isolated_home / ".a2x_registry_client" / "cli_token.json"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text("garbage", encoding="utf-8")
    result = read_cli_token(cfg)
    assert result is None
    # A warning was emitted (don't assert exact text — implementation detail).
    assert len(recwarn) >= 1


def test_read_non_dict_json_returns_none(isolated_home, recwarn):
    cfg = isolated_home / ".a2x_registry_client" / "cli_token.json"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text(json.dumps(["a", "b"]), encoding="utf-8")
    result = read_cli_token(cfg)
    assert result is None
