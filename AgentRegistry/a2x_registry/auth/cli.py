"""CLI subcommands for auth bootstrap.

Wired into ``a2x-registry`` via ``backend/__main__.py``: when the first
positional arg is ``auth``, dispatch flows here. Subcommands:

    a2x-registry auth init               — first-run bootstrap
    a2x-registry auth reset-admin --confirm
                                          — rotate root admin key

Both write to ``auth_data/`` directly (no HTTP, no running server). The
plaintext admin token is printed to **stderr** so it doesn't get
interleaved with stdout-piped scripts, and prefixed with a multiline
banner that's easy to copy-paste out of a terminal.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional

from .store import AuthStore, default_data_dir, PRINCIPALS_FILE, KEYS_FILE


_BANNER = """
============================================================
First-run bootstrap admin key (save now, will not be shown again):

    {token}

  scope:   admin  (namespaces=None → all)
  stored:  {data_dir}/{principals_file}
           {data_dir}/{keys_file}

Next:
  • set this token in your client via 'a2x-registry-client login'
  • or pass it explicitly: A2XRegistryClient(api_key='a2x_pat_…')
============================================================
"""


def _resolve_data_dir(args: argparse.Namespace) -> Path:
    if getattr(args, "data_dir", None):
        return Path(args.data_dir).resolve()
    return default_data_dir()


def cmd_init(args: argparse.Namespace) -> int:
    data_dir = _resolve_data_dir(args)
    try:
        store, token = AuthStore.bootstrap(
            data_dir=data_dir,
            admin_token=args.admin_token,  # None → store generates
            admin_handle=args.handle,
        )
    except FileExistsError as exc:
        print(f"\nauth init failed: {exc}\n", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"\nauth init failed: {exc}\n", file=sys.stderr)
        return 2
    print(
        _BANNER.format(
            token=token,
            data_dir=data_dir,
            principals_file=PRINCIPALS_FILE,
            keys_file=KEYS_FILE,
        ),
        file=sys.stderr,
    )
    return 0


def cmd_reset_admin(args: argparse.Namespace) -> int:
    """Rotate the bootstrap admin token. Hard rewrite — destroys existing data.

    Requires ``--confirm`` because it wipes the existing auth_data directory.
    All previously-issued tokens become invalid. Provider/user principals
    are NOT preserved by design — if you've lost the admin token, you've
    lost the chain of trust, and a clean reset is the only safe recovery.
    """
    if not args.confirm:
        print(
            "\nauth reset-admin requires --confirm. This destroys all existing\n"
            "principals/keys (every issued token becomes invalid).\n",
            file=sys.stderr,
        )
        return 2
    data_dir = _resolve_data_dir(args)
    # Wipe known files. Don't rmtree the whole dir — operator may have put
    # other stuff there. Just nuke our two state files + audit (rotation
    # treats the prior log as historical: roll, don't append).
    for fname in (PRINCIPALS_FILE, KEYS_FILE):
        p = data_dir / fname
        if p.exists():
            p.unlink()
    # Audit log is left in place; the new bootstrap appends to it as a
    # continuation record. (Audit history of the prior tenancy is still
    # the most useful evidence of what happened.)
    try:
        store, token = AuthStore.bootstrap(
            data_dir=data_dir,
            admin_token=args.admin_token,
            admin_handle=args.handle,
        )
    except (FileExistsError, ValueError) as exc:
        print(f"\nauth reset-admin failed: {exc}\n", file=sys.stderr)
        return 1
    print(
        _BANNER.format(
            token=token,
            data_dir=data_dir,
            principals_file=PRINCIPALS_FILE,
            keys_file=KEYS_FILE,
        ),
        file=sys.stderr,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    """Build the ``a2x-registry auth ...`` subparser tree.

    Returned as a standalone ArgumentParser so the parent CLI in
    ``backend/__main__`` can either delegate the whole ``auth`` argv slice
    to ``main(argv)`` below, or embed this as a subparser.
    """
    parser = argparse.ArgumentParser(
        prog="a2x-registry auth",
        description="A2X Registry — authentication module CLI",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_init = sub.add_parser(
        "init",
        help="Bootstrap the auth module (creates the first admin + key).",
    )
    p_init.add_argument(
        "--handle", default="root",
        help="Handle for the bootstrap admin principal (default: 'root')",
    )
    p_init.add_argument(
        "--admin-token", default=None,
        help=(
            "Use this exact plaintext token (must start with 'a2x_pat_'). "
            "For CI / scripted bootstraps; usually omit to let the store generate one."
        ),
    )
    p_init.add_argument(
        "--data-dir", default=None,
        help="Override the auth_data directory (else uses $A2X_REGISTRY_AUTH_DATA or the bundled path).",
    )
    p_init.set_defaults(func=cmd_init)

    p_reset = sub.add_parser(
        "reset-admin",
        help="Wipe principals/keys and create a new bootstrap admin.",
    )
    p_reset.add_argument("--confirm", action="store_true", required=False)
    p_reset.add_argument("--handle", default="root")
    p_reset.add_argument("--admin-token", default=None)
    p_reset.add_argument("--data-dir", default=None)
    p_reset.set_defaults(func=cmd_reset_admin)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    """Entry point invoked from ``a2x-registry auth ...`` dispatcher."""
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)
