"""Precedence rules: explicit args > cli_token.json > defaults."""

from __future__ import annotations

from a2x_registry_client.auth import (
    DEFAULT_BASE_URL,
    resolve_credentials,
    write_cli_token,
)


def test_no_config_no_explicit_returns_defaults(isolated_home):
    api_key, base_url = resolve_credentials(None, None)
    assert api_key is None
    assert base_url == DEFAULT_BASE_URL


def test_file_provides_both_fields(isolated_home):
    write_cli_token("a2x_pat_test_x" * 3, "http://from-file.example/")
    api_key, base_url = resolve_credentials(None, None)
    assert api_key.startswith("a2x_pat_")
    assert base_url == "http://from-file.example/"


def test_explicit_api_key_overrides_file(isolated_home):
    write_cli_token("a2x_pat_FILE" + "x" * 39, "http://from-file/")
    api_key, base_url = resolve_credentials("a2x_pat_EXPLICIT" + "y" * 35, None)
    assert api_key.startswith("a2x_pat_EXPLICIT")
    # base_url still comes from file
    assert base_url == "http://from-file/"


def test_explicit_base_url_overrides_file(isolated_home):
    write_cli_token("a2x_pat_x" * 6, "http://from-file/")
    api_key, base_url = resolve_credentials(None, "http://from-args/")
    assert api_key.startswith("a2x_pat_")  # token still from file
    assert base_url == "http://from-args/"


def test_both_explicit_bypasses_file_read(isolated_home, monkeypatch):
    """When both args are explicit, the file isn't read — important for
    test code that doesn't want to trip the file-perm warning."""
    sentinel_called = {"v": False}

    def _spy(*_a, **_kw):
        sentinel_called["v"] = True
        return None

    monkeypatch.setattr("a2x_registry_client.auth.read_cli_token", _spy)
    resolve_credentials("a2x_pat_x" * 6, "http://x/")
    assert sentinel_called["v"] is False


def test_explicit_args_dont_lose_when_file_corrupt(isolated_home):
    """If cli_token.json is corrupt, explicit args still resolve cleanly."""
    cfg = isolated_home / ".a2x_registry_client" / "cli_token.json"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text("not valid json {{", encoding="utf-8")
    api_key, base_url = resolve_credentials("a2x_pat_x" * 6, "http://x/")
    assert api_key.startswith("a2x_pat_")
    assert base_url == "http://x/"
