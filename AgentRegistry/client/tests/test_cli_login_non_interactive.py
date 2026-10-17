"""``login --token`` is the scripted path: it must not block on stdin."""

from __future__ import annotations

import io
import sys

from a2x_registry_client import cli as cli_mod
from a2x_registry_client.auth import read_cli_token
from a2x_registry_client.errors import A2XConnectionError


class _OfflineTransport:
    def request(self, *args, **kwargs):
        raise A2XConnectionError("offline")


class _OfflineClient:
    def __init__(self, *args, **kwargs):
        self._transport = _OfflineTransport()

    def close(self) -> None:
        pass


def test_login_with_token_does_not_prompt_without_tty(isolated_home, monkeypatch):
    # A closed/redirected stdin is exactly what a CI or provisioning script has.
    monkeypatch.setattr(sys, "stdin", io.StringIO(""))
    monkeypatch.setattr(cli_mod, "A2XRegistryClient", _OfflineClient)

    args = cli_mod.build_parser().parse_args(
        ["login", "--token", "a2x_pat_probe123"],
    )
    assert cli_mod.cmd_login(args) == 0

    config = read_cli_token()
    assert config is not None
    assert config["api_key"] == "a2x_pat_probe123"
    assert config["base_url"] == cli_mod.DEFAULT_BASE_URL


def test_login_keeps_explicit_base_url(isolated_home, monkeypatch):
    monkeypatch.setattr(sys, "stdin", io.StringIO(""))
    monkeypatch.setattr(cli_mod, "A2XRegistryClient", _OfflineClient)

    args = cli_mod.build_parser().parse_args([
        "login", "--token", "a2x_pat_probe123",
        "--base-url", "https://registry.example",
    ])
    assert cli_mod.cmd_login(args) == 0

    assert read_cli_token()["base_url"] == "https://registry.example"
