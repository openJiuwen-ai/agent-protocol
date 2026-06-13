"""Shared fixtures for the client-SDK tests.

These tests never hit a live server — they use ``httpx.MockTransport`` to
stub the registry responses and verify SDK behavior in isolation.
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterator

import pytest


@pytest.fixture
def isolated_home(tmp_path, monkeypatch) -> Iterator[Path]:
    """Point ``Path.home()`` at a fresh tmp dir for this test.

    The SDK's credential resolution reads ``~/.a2x_registry_client/cli_token.json``.
    Without isolation, a test that writes a token would clobber the
    developer's real credentials. Patches happen on ``pathlib.Path.home``
    so any code path that recomputes the default config path picks up
    the redirect.
    """
    fake_home = tmp_path / "home"
    fake_home.mkdir()

    def _fake_home():
        return fake_home

    monkeypatch.setattr(Path, "home", staticmethod(_fake_home))
    # The SDK already-imported DEFAULT_CONFIG_PATH constant was bound at
    # module-load time; patch it to the new home as well.
    import a2x_registry_client.auth as auth_mod
    monkeypatch.setattr(
        auth_mod,
        "DEFAULT_CONFIG_PATH",
        fake_home / ".a2x_registry_client" / "cli_token.json",
    )
    yield fake_home
