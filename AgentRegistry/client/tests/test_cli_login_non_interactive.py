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


def test_login_keeps_a_base_url_given_before_the_subcommand(isolated_home, monkeypatch):
    """The top-level --base-url must survive the login subparser's defaults."""
    monkeypatch.setattr(sys, "stdin", io.StringIO(""))
    monkeypatch.setattr(cli_mod, "A2XRegistryClient", _OfflineClient)

    args = cli_mod.build_parser().parse_args([
        "--base-url", "https://registry.example",
        "login", "--token", "a2x_pat_probe123",
    ])
    assert cli_mod.cmd_login(args) == 0

    assert read_cli_token()["base_url"] == "https://registry.example"


class _TtyEof(io.StringIO):
    """A stdin that claims to be a terminal but has no input, like Windows `< NUL`."""

    def isatty(self) -> bool:
        return True


def test_login_without_token_reads_url_and_token_from_a_pipe(isolated_home, monkeypatch):
    """Without --token, both prompts still read stdin, so piping both lines works."""
    monkeypatch.setattr(sys, "stdin", io.StringIO("https://registry.example\na2x_pat_piped\n"))
    # getpass falls back to reading stdin when no terminal is available.
    monkeypatch.setattr(cli_mod.getpass, "getpass", lambda prompt="": sys.stdin.readline())
    monkeypatch.setattr(cli_mod, "A2XRegistryClient", _OfflineClient)

    args = cli_mod.build_parser().parse_args(["login"])
    assert cli_mod.cmd_login(args) == 0

    config = read_cli_token()
    assert config["base_url"] == "https://registry.example"
    assert config["api_key"] == "a2x_pat_piped"


def test_login_with_token_tolerates_a_closed_stdin(isolated_home, monkeypatch):
    monkeypatch.setattr(sys, "stdin", None)
    monkeypatch.setattr(cli_mod, "A2XRegistryClient", _OfflineClient)

    args = cli_mod.build_parser().parse_args(["login", "--token", "a2x_pat_probe123"])
    assert cli_mod.cmd_login(args) == 0

    assert read_cli_token()["base_url"] == cli_mod.DEFAULT_BASE_URL


def test_login_falls_back_when_a_tty_stdin_hits_eof(isolated_home, monkeypatch, capsys):
    monkeypatch.setattr(sys, "stdin", _TtyEof(""))
    monkeypatch.setattr(cli_mod, "A2XRegistryClient", _OfflineClient)

    args = cli_mod.build_parser().parse_args(["login", "--token", "a2x_pat_probe123"])
    assert cli_mod.cmd_login(args) == 0

    assert read_cli_token()["base_url"] == cli_mod.DEFAULT_BASE_URL
    assert "no --base-url given" in capsys.readouterr().err
