"""CLI for the A2X Registry client SDK.

Subcommands:
    a2x-registry-client login [--base-url URL]      Interactive paste → cli_token.json
    a2x-registry-client logout                      Remove the local token file
    a2x-registry-client whoami                      GET /api/auth/whoami
    a2x-registry-client keys list                   List own keys (admin sees all)
    a2x-registry-client keys create --name NAME     Issue new key for self
    a2x-registry-client keys revoke KEY_ID          Revoke a key

All commands honor ``--base-url`` to override the value in cli_token.json
or the SDK default. Authentication for ``whoami`` / ``keys *`` uses the
token from cli_token.json — run ``login`` first.
"""

from __future__ import annotations

import argparse
import getpass
import json
import sys
from typing import List, Optional

from .auth import (
    DEFAULT_BASE_URL,
    DEFAULT_CONFIG_PATH,
    read_cli_token,
    remove_cli_token,
    write_cli_token,
)
from .client import A2XRegistryClient
from .errors import A2XAuthenticationError, A2XAuthorizationError, A2XError


def _client_or_die(base_url: Optional[str] = None) -> A2XRegistryClient:
    """Build a client honoring the saved token. Exits 2 if no token exists
    and the operation needs one (we re-raise from inside the command).
    """
    cfg = read_cli_token() or {}
    return A2XRegistryClient(
        base_url=base_url or cfg.get("base_url"),
        api_key=cfg.get("api_key"),
    )


def cmd_login(args: argparse.Namespace) -> int:
    base_url = args.base_url or input(f"Registry URL [{DEFAULT_BASE_URL}]: ").strip() or DEFAULT_BASE_URL
    if args.token:
        token = args.token
    else:
        # echo-off prompt — getpass also handles redirected stdin gracefully
        token = getpass.getpass("API key: ").strip()
    if not token.startswith("a2x_pat_"):
        print("error: token must start with 'a2x_pat_'", file=sys.stderr)
        return 2
    try:
        path = write_cli_token(api_key=token, base_url=base_url)
    except (OSError, ValueError) as exc:
        print(f"error: failed to write {DEFAULT_CONFIG_PATH}: {exc}", file=sys.stderr)
        return 1
    # Sanity-poke /whoami to fail loudly if the token doesn't actually work,
    # rather than letting the user discover that later.
    client = A2XRegistryClient(base_url=base_url, api_key=token)
    try:
        r = client._transport.request("GET", "/api/auth/whoami")
        who = r.json()
        print(f"✓ Saved to {path}")
        print(f"  Logged in as {who.get('handle')!r} (role={who.get('role')})")
        return 0
    except A2XAuthenticationError as exc:
        print(f"warning: token saved but server says 401: {exc}", file=sys.stderr)
        return 1
    except A2XError as exc:
        print(f"warning: token saved but whoami failed: {exc}", file=sys.stderr)
        return 0  # still saved; caller may be running pre-bootstrap test
    finally:
        client.close()


def cmd_logout(_args: argparse.Namespace) -> int:
    removed = remove_cli_token()
    if removed:
        print(f"✓ Removed {DEFAULT_CONFIG_PATH}")
    else:
        print(f"(no token file at {DEFAULT_CONFIG_PATH})")
    return 0


def cmd_whoami(args: argparse.Namespace) -> int:
    client = _client_or_die(args.base_url)
    try:
        r = client._transport.request("GET", "/api/auth/whoami")
        print(json.dumps(r.json(), indent=2, ensure_ascii=False))
        return 0
    except A2XError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()


def cmd_keys_list(args: argparse.Namespace) -> int:
    client = _client_or_die(args.base_url)
    try:
        r = client._transport.request("GET", "/api/auth/keys")
        for k in r.json():
            revoked = f"  REVOKED({k.get('revoked_at')})" if k.get("revoked_at") else ""
            print(f"{k['key_id']}  {k['key_prefix']}…  name={k.get('name')!r}{revoked}")
        return 0
    except A2XError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()


def cmd_keys_create(args: argparse.Namespace) -> int:
    client = _client_or_die(args.base_url)
    try:
        r = client._transport.request("POST", "/api/auth/keys", json={"name": args.name})
        body = r.json()
        # PLAINTEXT printed to stdout exactly once. The user is expected to
        # capture this immediately into their secrets store.
        print(f"key_id:    {body['key_id']}")
        print(f"prefix:    {body['key_prefix']}")
        print(f"token:     {body['token']}   ← save now, will not be shown again")
        return 0
    except A2XError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()


def cmd_keys_revoke(args: argparse.Namespace) -> int:
    client = _client_or_die(args.base_url)
    try:
        r = client._transport.request("DELETE", f"/api/auth/keys/{args.key_id}")
        body = r.json()
        print(f"✓ Revoked {body.get('key_id')} at {body.get('revoked_at')}")
        return 0
    except A2XAuthorizationError as exc:
        print(f"error: 403 {exc}", file=sys.stderr)
        return 3
    except A2XError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="a2x-registry-client",
        description="A2X Registry — client SDK CLI",
    )
    parser.add_argument(
        "--base-url", default=None,
        help="Override the registry URL for this invocation (else uses cli_token.json)",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_login = sub.add_parser("login", help="Interactive paste → cli_token.json")
    p_login.add_argument("--base-url", default=None, dest="base_url")
    p_login.add_argument(
        "--token", default=None,
        help="Use this token instead of prompting (for scripts; less secure).",
    )
    p_login.set_defaults(func=cmd_login)

    sub.add_parser("logout", help="Remove cli_token.json").set_defaults(func=cmd_logout)
    sub.add_parser("whoami", help="GET /api/auth/whoami").set_defaults(func=cmd_whoami)

    p_keys = sub.add_parser("keys", help="Manage API keys for the current principal")
    keys_sub = p_keys.add_subparsers(dest="keys_cmd", required=True)
    keys_sub.add_parser("list", help="List own keys").set_defaults(func=cmd_keys_list)
    p_create = keys_sub.add_parser("create", help="Issue a new key")
    p_create.add_argument("--name", default="cli", help="Human label, e.g. 'laptop'")
    p_create.set_defaults(func=cmd_keys_create)
    p_revoke = keys_sub.add_parser("revoke", help="Revoke a key")
    p_revoke.add_argument("key_id")
    p_revoke.set_defaults(func=cmd_keys_revoke)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
