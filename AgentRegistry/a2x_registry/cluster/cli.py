"""``a2x-registry cluster ...`` CLI.

The primary, user-facing way to drive distributed sync. Dispatched from
``backend/__main__.py`` when argv starts with ``cluster``.

Commands (M0):
  init     — generate this instance's node id + cluster_state.json (opt-in
             switch; the running server picks it up on next start).
  status   — pretty-print GET /api/cluster/state from a running server.

``add-peer`` / ``rm-peer`` arrive with the session milestone. ``status``
talks to the local server over HTTP (cross-platform; no OS-specific IPC).
"""

from __future__ import annotations

import argparse
import json
from typing import List, Optional

from .state import ClusterState, state_path

DEFAULT_SERVER = "http://127.0.0.1:8000"


def cmd_init(args: argparse.Namespace) -> int:
    try:
        state = ClusterState.init(node_id=args.node_id)
    except FileExistsError as exc:
        print(f"error: {exc}")
        return 1
    print(f"Cluster initialized.")
    print(f"  node_id : {state.node_id}")
    print(f"  state   : {state.path}")
    print("Restart the registry server for the cluster module to load.")
    return 0


def _client_call(server: str, method: str, path: str, **kw):
    """Call the local server, returning (resp_or_None, error_str_or_None).

    trust_env=False → ignore system proxies so localhost isn't intercepted
    (Clash/VPN on Windows).
    """
    import httpx

    url = server.rstrip("/") + path
    try:
        with httpx.Client(trust_env=False, timeout=10.0) as client:
            return client.request(method, url, **kw), None
    except httpx.HTTPError as exc:
        return None, f"cannot reach server at {server}: {exc}"


def _print_resp_or_404(resp) -> int:
    if resp.status_code == 404:
        print("Cluster module not initialized on the server "
              "(run 'a2x-registry cluster init', then restart the server).")
        return 1
    if resp.status_code // 100 != 2:
        print(f"error: server returned {resp.status_code}: {resp.text}")
        return 1
    print(json.dumps(resp.json(), ensure_ascii=False, indent=2))
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    resp, err = _client_call(args.server, "GET", "/api/cluster/state")
    if err:
        print(f"error: {err}")
        return 1
    return _print_resp_or_404(resp)


def cmd_add_peer(args: argparse.Namespace) -> int:
    namespaces = (
        [n for n in args.namespaces.split(",") if n] if args.namespaces else None
    )
    body = {"address": args.address, "namespaces": namespaces, "token": args.token}
    resp, err = _client_call(args.server, "POST", "/api/cluster/peers", json=body)
    if err:
        print(f"error: {err}")
        return 1
    return _print_resp_or_404(resp)


def cmd_rm_peer(args: argparse.Namespace) -> int:
    resp, err = _client_call(
        args.server, "DELETE", f"/api/cluster/peers/{args.node_id}",
    )
    if err:
        print(f"error: {err}")
        return 1
    return _print_resp_or_404(resp)


def cmd_set_add(args: argparse.Namespace) -> int:
    body = {
        "members": [{"address": a} for a in args.addresses],
        "token": args.token,
    }
    resp, err = _client_call(args.server, "POST", "/api/cluster/set/add", json=body)
    if err:
        print(f"error: {err}")
        return 1
    return _print_resp_or_404(resp)


def cmd_set_remove(args: argparse.Namespace) -> int:
    body = {"members": [{"node_id": n} for n in args.node_ids]}
    resp, err = _client_call(args.server, "POST", "/api/cluster/set/remove", json=body)
    if err:
        print(f"error: {err}")
        return 1
    return _print_resp_or_404(resp)


def cmd_set_show(args: argparse.Namespace) -> int:
    resp, err = _client_call(args.server, "GET", "/api/cluster/set")
    if err:
        print(f"error: {err}")
        return 1
    return _print_resp_or_404(resp)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="a2x-registry cluster")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_init = sub.add_parser("init", help="Generate node id + cluster_state.json")
    p_init.add_argument("--node-id", default=None,
                        help="Explicit node id (default: auto-generated UUID)")
    p_init.set_defaults(func=cmd_init)

    p_status = sub.add_parser("status", help="Show sync state from a running server")
    p_status.add_argument("--server", default=DEFAULT_SERVER,
                          help=f"Registry base URL (default: {DEFAULT_SERVER})")
    p_status.set_defaults(func=cmd_status)

    p_add = sub.add_parser("add-peer", help="Connect to a peer and sync (primary trigger)")
    p_add.add_argument("address", help="Peer base URL, e.g. http://10.0.0.2:8000")
    p_add.add_argument("--namespaces", default=None,
                       help="Comma-separated namespaces to sync (default: all)")
    p_add.add_argument("--token", default=None,
                       help="API key for the peer's per-namespace authorization")
    p_add.add_argument("--server", default=DEFAULT_SERVER,
                       help=f"This instance's base URL (default: {DEFAULT_SERVER})")
    p_add.set_defaults(func=cmd_add_peer)

    p_rm = sub.add_parser("rm-peer", help="Drop a peer session + its replicated records")
    p_rm.add_argument("node_id", help="Peer node id to disconnect")
    p_rm.add_argument("--server", default=DEFAULT_SERVER,
                      help=f"This instance's base URL (default: {DEFAULT_SERVER})")
    p_rm.set_defaults(func=cmd_rm_peer)

    # ── declarative membership (primary user interface) ──────────────────
    p_set = sub.add_parser("set", help="Declaratively manage cluster membership")
    set_sub = p_set.add_subparsers(dest="set_cmd", required=True)

    p_set_add = set_sub.add_parser(
        "add", help="Add members to this node's cluster (auto full-mesh)")
    p_set_add.add_argument("addresses", nargs="+",
                           help="Member base URLs, e.g. http://10.0.0.2:8000")
    p_set_add.add_argument("--token", default=None,
                           help="Admin API key, if the members require auth")
    p_set_add.add_argument("--server", default=DEFAULT_SERVER,
                           help=f"This instance's base URL (default: {DEFAULT_SERVER})")
    p_set_add.set_defaults(func=cmd_set_add)

    p_set_rm = set_sub.add_parser("remove", help="Remove members from the cluster")
    p_set_rm.add_argument("node_ids", nargs="+", help="Member node ids to remove")
    p_set_rm.add_argument("--server", default=DEFAULT_SERVER,
                          help=f"This instance's base URL (default: {DEFAULT_SERVER})")
    p_set_rm.set_defaults(func=cmd_set_remove)

    p_set_show = set_sub.add_parser("show", help="Show this node's cluster + roster")
    p_set_show.add_argument("--server", default=DEFAULT_SERVER,
                            help=f"This instance's base URL (default: {DEFAULT_SERVER})")
    p_set_show.set_defaults(func=cmd_set_show)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    """Entry point invoked from ``a2x-registry cluster ...`` dispatcher."""
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)
