"""CLI for the registration module.

Usage (subcommands):
    python -m a2x_registry.register status [--dataset DS]
    python -m a2x_registry.register datasets
    python -m a2x_registry.register list DATASET [--mode browse|admin]
    python -m a2x_registry.register get DATASET SERVICE_ID
    python -m a2x_registry.register register-generic DATASET --name NAME --desc DESC [--url URL]
    python -m a2x_registry.register register-a2a DATASET (--url URL | --card-file FILE)
    python -m a2x_registry.register register-skill DATASET ZIP_FILE
    python -m a2x_registry.register update DATASET SERVICE_ID [--json FILE | --set k=v ...] [--name N] [--desc D] [--url U] [--license L]
    python -m a2x_registry.register deregister DATASET SERVICE_ID
    python -m a2x_registry.register deregister-skill DATASET NAME
    python -m a2x_registry.register create-dataset NAME [--embedding-model MODEL] [--formats SPEC]
    python -m a2x_registry.register delete-dataset NAME [--confirm]
    python -m a2x_registry.register get-register-config DATASET
    python -m a2x_registry.register set-register-config DATASET --formats SPEC

Legacy usage (still supported):
    python -m a2x_registry.register --config path/to/config.json
    python -m a2x_registry.register --status [--dataset DS]
"""

import argparse
import json
import logging
import sys
from pathlib import Path

from a2x_registry.common.paths import database_dir, get_home

from .errors import RegistryNotFoundError
from .models import AgentCard, RegisterA2ARequest, RegisterGenericRequest
from .service import RegistryService

PROJECT_ROOT = get_home()
DATABASE_DIR = database_dir()

SUBCOMMANDS = {
    "status", "datasets", "create-dataset", "delete-dataset",
    "list", "get", "register-generic", "register-a2a",
    "register-skill", "deregister", "deregister-skill",
    "get-register-config", "set-register-config",
    "update",
}


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def _print_json(data):
    print(json.dumps(data, indent=2, ensure_ascii=False))


def _print_table(headers: list, rows: list):
    if not rows:
        return
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(str(cell)))
    fmt = "  ".join(f"{{:<{w}}}" for w in widths)
    print("  " + fmt.format(*headers))
    for row in rows:
        print("  " + fmt.format(*[str(c) for c in row]))


def _print_kv(pairs: list):
    if not pairs:
        return
    max_key = max(len(k) for k, _ in pairs)
    for key, val in pairs:
        print(f"  {key + ':':<{max_key + 2}} {val}")


# ---------------------------------------------------------------------------
# Subcommand handlers
# ---------------------------------------------------------------------------

def _cmd_status(service: RegistryService, args):
    status = service.get_status(args.dataset)
    if args.json_output:
        _print_json(status.model_dump())
        return
    print("Registry Status")
    _print_kv([
        ("Total services", str(status.total_services)),
        ("Datasets", ", ".join(status.datasets) if status.datasets else "(none)"),
    ])
    if status.by_source:
        print("\n  By source:")
        for src, count in sorted(status.by_source.items()):
            print(f"    {src:<16} {count}")


def _cmd_datasets(service: RegistryService, args):
    ds_list = service.list_datasets()
    if args.json_output:
        _print_json(ds_list)
        return
    if not ds_list:
        print("No datasets found.")
        return
    print("Datasets:")
    for name in ds_list:
        count = len(service.list_services(name))
        print(f"  {name} ({count} services)")


def _cmd_create_dataset(service: RegistryService, args):
    formats = _parse_formats_spec(args.formats) if args.formats else None
    service.create_dataset(args.name, args.embedding_model, formats=formats)
    effective = service.get_register_config(args.name)
    if args.json_output:
        _print_json({"dataset": args.name, "embedding_model": args.embedding_model,
                     "formats": effective, "status": "created"})
        return
    print(f"Created dataset '{args.name}' (embedding: {args.embedding_model})")
    print("  Allowed formats:")
    for t, v in sorted(effective.items()):
        print(f"    {t:<8} min_version={v}")


def _cmd_get_register_config(service: RegistryService, args):
    cfg = service.get_register_config(args.dataset)
    if args.json_output:
        _print_json({"dataset": args.dataset, "formats": cfg})
        return
    print(f"Register config for '{args.dataset}':")
    for t, v in sorted(cfg.items()):
        print(f"  {t:<8} min_version={v}")


def _cmd_set_register_config(service: RegistryService, args):
    formats = _parse_formats_spec(args.formats)
    cfg = service.set_register_config(args.dataset, formats)
    if args.json_output:
        _print_json({"dataset": args.dataset, "formats": cfg})
        return
    print(f"Updated register config for '{args.dataset}':")
    for t, v in sorted(cfg.items()):
        print(f"  {t:<8} min_version={v}")


def _parse_formats_spec(spec: str) -> dict:
    """Parse a comma-separated formats spec.

    Examples:
      "generic,a2a,skill"                   → all types at v0.0
      "generic:v0.0,a2a:v1.0"               → explicit versions
      "a2a:v1.0"                            → single type, only v1.0+
    Unknown types / versions are silently dropped downstream.
    """
    out = {}
    for piece in spec.split(","):
        piece = piece.strip()
        if not piece:
            continue
        if ":" in piece:
            t, v = piece.split(":", 1)
            out[t.strip()] = v.strip()
        else:
            out[piece] = "v0.0"
    return out


def _cmd_delete_dataset(service: RegistryService, args):
    if not args.confirm:
        if not sys.stdin.isatty():
            print("Error: --confirm required for non-interactive use", file=sys.stderr)
            sys.exit(1)
        answer = input(f"Delete dataset '{args.name}' and all its data? [y/N] ")
        if answer.lower() != "y":
            print("Aborted.")
            return
    service.delete_dataset(args.name)
    if args.json_output:
        _print_json({"dataset": args.name, "status": "deleted"})
        return
    print(f"Deleted dataset '{args.name}'")


def _cmd_list(service: RegistryService, args):
    if args.mode == "browse":
        services = service.list_services(args.dataset)
        if args.json_output:
            _print_json(services)
            return
        if not services:
            print(f"No services in '{args.dataset}'.")
            return
        print(f"Services in '{args.dataset}' ({len(services)} total)\n")
        rows = [[s["id"], s["name"], _truncate(s.get("description", ""), 60)] for s in services]
        _print_table(["ID", "NAME", "DESCRIPTION"], rows)
    else:
        entries = sorted(service.list_entries(args.dataset), key=lambda e: e.service_id)
        if args.json_output:
            _print_json([e.model_dump(exclude_none=True) for e in entries])
            return
        if not entries:
            print(f"No services in '{args.dataset}'.")
            return
        print(f"Services in '{args.dataset}' ({len(entries)} total)\n")
        rows = []
        for e in entries:
            name = _entry_name(e)
            rows.append([e.service_id, e.type, e.source, name])
        _print_table(["ID", "TYPE", "SOURCE", "NAME"], rows)


def _cmd_get(service: RegistryService, args):
    entry = service.get_entry(args.dataset, args.service_id)
    if entry is None:
        print(f"Service '{args.service_id}' not found in '{args.dataset}'.", file=sys.stderr)
        sys.exit(1)
    if args.json_output:
        _print_json(entry.model_dump(exclude_none=True))
        return
    name = _entry_name(entry)
    desc = _entry_description(entry)
    print(f"Service: {entry.service_id}")
    _print_kv([
        ("Type", entry.type),
        ("Source", entry.source),
        ("Name", name),
        ("Description", desc),
    ])
    if entry.type == "generic" and entry.service_data:
        if entry.service_data.url:
            _print_kv([("URL", entry.service_data.url)])
        if entry.service_data.inputSchema:
            print(f"\n  inputSchema:\n{json.dumps(entry.service_data.inputSchema, indent=4, ensure_ascii=False)}")
    elif entry.type == "a2a" and entry.agent_card:
        if entry.agent_card.url:
            _print_kv([("Agent URL", entry.agent_card.url)])
        if entry.agent_card_url:
            _print_kv([("Card URL", entry.agent_card_url)])
    elif entry.type == "skill" and entry.skill_data:
        _print_kv([
            ("Skill path", entry.skill_data.skill_path),
            ("Files", ", ".join(entry.skill_data.files) if entry.skill_data.files else "(none)"),
        ])


def _cmd_register_generic(service: RegistryService, args):
    input_schema = {}
    if args.input_schema:
        schema_path = Path(args.input_schema)
        if not schema_path.exists():
            print(f"Error: File not found: {schema_path}", file=sys.stderr)
            sys.exit(1)
        with open(schema_path, "r", encoding="utf-8") as f:
            input_schema = json.load(f)

    req = RegisterGenericRequest(
        dataset=args.dataset,
        name=args.name,
        description=args.description,
        url=args.url or "",
        inputSchema=input_schema,
        service_id=args.service_id,
    )
    resp = service.register_generic(req)
    if args.json_output:
        _print_json(resp.model_dump())
        return
    print(f"Registered generic service")
    _print_kv([
        ("ID", resp.service_id),
        ("Dataset", resp.dataset),
        ("Status", resp.status),
    ])


def _cmd_register_a2a(service: RegistryService, args):
    agent_card = None
    agent_card_url = None
    if args.card_file:
        card_path = Path(args.card_file)
        if not card_path.exists():
            print(f"Error: File not found: {card_path}", file=sys.stderr)
            sys.exit(1)
        with open(card_path, "r", encoding="utf-8") as f:
            agent_card = AgentCard(**json.load(f))
    else:
        agent_card_url = args.agent_card_url

    req = RegisterA2ARequest(
        dataset=args.dataset,
        agent_card=agent_card,
        agent_card_url=agent_card_url,
        service_id=args.service_id,
    )
    resp = service.register_a2a(req)
    if args.json_output:
        _print_json(resp.model_dump())
        return
    print(f"Registered A2A agent")
    _print_kv([
        ("ID", resp.service_id),
        ("Dataset", resp.dataset),
        ("Status", resp.status),
    ])


def _cmd_register_skill(service: RegistryService, args):
    zip_path = Path(args.zip_file)
    if not zip_path.exists():
        print(f"Error: File not found: {zip_path}", file=sys.stderr)
        sys.exit(1)
    zip_bytes = zip_path.read_bytes()
    resp = service.register_skill(args.dataset, zip_bytes)
    if args.json_output:
        _print_json(resp.model_dump())
        return
    print(f"Registered skill")
    _print_kv([
        ("Name", resp.name),
        ("ID", resp.service_id),
        ("Dataset", resp.dataset),
        ("Status", resp.status),
    ])


def _cmd_update(service: RegistryService, args):
    """Partial-update a service. Accepts --json FILE, --set key=value (repeatable),
    and shortcut flags (--name / --description / --url / --license)."""
    updates: dict = {}
    if args.json_file:
        path = Path(args.json_file)
        if not path.exists():
            print(f"Error: File not found: {path}", file=sys.stderr)
            sys.exit(1)
        with open(path, "r", encoding="utf-8") as f:
            loaded = json.load(f)
        if not isinstance(loaded, dict):
            print("Error: --json file must contain a JSON object", file=sys.stderr)
            sys.exit(1)
        updates.update(loaded)

    for kv in args.set or []:
        if "=" not in kv:
            print(f"Error: --set expects key=value, got {kv!r}", file=sys.stderr)
            sys.exit(1)
        k, v = kv.split("=", 1)
        updates[k.strip()] = v

    # Shortcut flags override --set / --json for the same key (most specific wins).
    if args.name is not None:        updates["name"] = args.name
    if args.description is not None: updates["description"] = args.description
    if args.url is not None:         updates["url"] = args.url
    if args.license is not None:     updates["license"] = args.license

    if not updates:
        print("Error: no updates provided (use --json, --set, or shortcut flags)",
              file=sys.stderr)
        sys.exit(1)

    resp = service.update_service(args.dataset, args.service_id, updates)
    if args.json_output:
        _print_json(resp.model_dump())
        return
    print(f"Updated service '{resp.service_id}' in '{resp.dataset}'")
    _print_kv([
        ("Changed fields", ", ".join(resp.changed_fields) or "(none — no-op)"),
        ("Taxonomy stale", "yes" if resp.taxonomy_affected else "no"),
    ])


def _cmd_deregister(service: RegistryService, args):
    try:
        resp = service.deregister(args.dataset, args.service_id)
    except RegistryNotFoundError as exc:
        if args.json_output:
            _print_json({"service_id": args.service_id, "status": "not_found"})
        else:
            print(str(exc), file=sys.stderr)
        sys.exit(1)
    if args.json_output:
        _print_json(resp.model_dump())
        return
    print(f"Deregistered service")
    _print_kv([
        ("ID", resp.service_id),
        ("Status", resp.status),
    ])


def _cmd_deregister_skill(service: RegistryService, args):
    resp = service.deregister_skill(args.dataset, args.name)
    if args.json_output:
        _print_json(resp.model_dump())
        return
    if resp.status == "not_found":
        print(f"Skill '{args.name}' not found in '{args.dataset}'.", file=sys.stderr)
        sys.exit(1)
    print(f"Deleted skill")
    _print_kv([
        ("Name", resp.name),
        ("ID", resp.service_id),
        ("Dataset", resp.dataset),
        ("Status", resp.status),
    ])


# ---------------------------------------------------------------------------
# Entry helpers
# ---------------------------------------------------------------------------

def _entry_name(entry) -> str:
    if entry.type == "generic" and entry.service_data:
        return entry.service_data.name
    if entry.type == "skill" and entry.skill_data:
        return entry.skill_data.name
    if entry.agent_card:
        return entry.agent_card.name
    return entry.service_id


def _entry_description(entry) -> str:
    if entry.type == "generic" and entry.service_data:
        return entry.service_data.description
    if entry.type == "skill" and entry.skill_data:
        return entry.skill_data.description
    if entry.agent_card:
        return entry.agent_card.description
    return ""


def _truncate(text: str, max_len: int) -> str:
    text = text.replace("\n", " ")
    if len(text) <= max_len:
        return text
    return text[: max_len - 3] + "..."


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m a2x_registry.register",
        description="A2X Registry CLI — manage datasets, services, and skills",
    )
    parser.add_argument("--database-dir", type=str, default=str(DATABASE_DIR),
                        help="Path to the database directory")
    parser.add_argument("--config", type=str, default=None,
                        help="Path to global config file (user_config.json)")
    parser.add_argument("--json", action="store_true", dest="json_output",
                        help="Output machine-readable JSON")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Enable DEBUG logging")

    sub = parser.add_subparsers(dest="command")

    # status
    p = sub.add_parser("status", help="Show registry status")
    p.add_argument("--dataset", type=str, default=None, help="Filter by dataset")

    # datasets
    sub.add_parser("datasets", help="List all datasets")

    # create-dataset
    p = sub.add_parser("create-dataset", help="Create a new dataset")
    p.add_argument("name", help="Dataset name")
    from a2x_registry.vector.utils.embedding import DEFAULT_EMBEDDING_MODEL
    p.add_argument("--embedding-model", default=DEFAULT_EMBEDDING_MODEL,
                   help=f"Embedding model (default: {DEFAULT_EMBEDDING_MODEL})")
    p.add_argument("--formats", default=None,
                   help="Comma-separated allowed formats. "
                        "Examples: 'generic,a2a,skill' (all v0.0) or "
                        "'generic:v0.0,a2a:v1.0'. Default: all three at v0.0.")

    # get-register-config
    p = sub.add_parser("get-register-config", help="Show allowed registration formats")
    p.add_argument("dataset", help="Dataset name")

    # set-register-config
    p = sub.add_parser("set-register-config", help="Replace allowed registration formats")
    p.add_argument("dataset", help="Dataset name")
    p.add_argument("--formats", required=True,
                   help="Comma-separated formats spec (e.g. 'generic,a2a:v1.0')")

    # delete-dataset
    p = sub.add_parser("delete-dataset", help="Delete a dataset")
    p.add_argument("name", help="Dataset name")
    p.add_argument("--confirm", action="store_true", help="Skip confirmation prompt")

    # list
    p = sub.add_parser("list", help="List services in a dataset")
    p.add_argument("dataset", help="Dataset name")
    p.add_argument("--mode", choices=["browse", "admin"], default="admin",
                   help="browse: lightweight; admin: with type/source (default: admin)")

    # get
    p = sub.add_parser("get", help="Get a single service entry")
    p.add_argument("dataset", help="Dataset name")
    p.add_argument("service_id", help="Service ID")

    # register-generic
    p = sub.add_parser("register-generic", help="Register a generic service")
    p.add_argument("dataset", help="Target dataset")
    p.add_argument("--name", required=True, help="Service name")
    p.add_argument("--description", "--desc", required=True, help="Service description")
    p.add_argument("--url", default="", help="Service URL")
    p.add_argument("--input-schema", type=str, default=None,
                   help="Path to JSON file with inputSchema")
    p.add_argument("--service-id", default=None, help="Explicit service ID")

    # register-a2a
    p = sub.add_parser("register-a2a", help="Register an A2A agent")
    p.add_argument("dataset", help="Target dataset")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--url", dest="agent_card_url",
                       help="URL to fetch the agent card from")
    group.add_argument("--card-file", type=str,
                       help="Path to a local agent card JSON file")
    p.add_argument("--service-id", default=None, help="Explicit service ID")

    # register-skill
    p = sub.add_parser("register-skill", help="Upload a skill ZIP")
    p.add_argument("dataset", help="Target dataset")
    p.add_argument("zip_file", help="Path to skill ZIP file")

    # update
    p = sub.add_parser("update", help="Partially update a service by ID")
    p.add_argument("dataset", help="Dataset name")
    p.add_argument("service_id", help="Service ID to update")
    p.add_argument("--json", dest="json_file", default=None,
                   help="Path to JSON file containing updates dict")
    p.add_argument("--set", action="append", default=None, metavar="KEY=VALUE",
                   help="Set a single top-level field (repeatable)")
    p.add_argument("--name", default=None, help="Shortcut: update name")
    p.add_argument("--description", "--desc", default=None,
                   help="Shortcut: update description")
    p.add_argument("--url", default=None, help="Shortcut: update url (generic/a2a)")
    p.add_argument("--license", default=None, help="Shortcut: update license (skill)")

    # deregister
    p = sub.add_parser("deregister", help="Deregister a service by ID")
    p.add_argument("dataset", help="Dataset name")
    p.add_argument("service_id", help="Service ID to remove")

    # deregister-skill
    p = sub.add_parser("deregister-skill", help="Remove a skill by name")
    p.add_argument("dataset", help="Dataset name")
    p.add_argument("name", help="Skill name")

    return parser


# ---------------------------------------------------------------------------
# Legacy compatibility
# ---------------------------------------------------------------------------

def _is_legacy_invocation(argv: list) -> bool:
    if any(arg in SUBCOMMANDS for arg in argv):
        return False
    # Only treat as legacy when old-style flags are explicitly present
    return "--status" in argv or ("--config" in argv and "-h" not in argv and "--help" not in argv)


def _handle_legacy(argv: list):
    """Handle old-style --status / --config invocations."""
    parser = argparse.ArgumentParser(description="A2X Registry (legacy)")
    parser.add_argument("--config", type=str, help="Path to global config file")
    parser.add_argument("--status", action="store_true", help="Show registry status")
    parser.add_argument("--dataset", type=str, default=None, help="Filter by dataset")
    parser.add_argument("--database-dir", type=str, default=str(DATABASE_DIR))
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    database_dir = Path(args.database_dir)
    config_path = Path(args.config) if args.config else None

    service = RegistryService(database_dir, config_path)
    changes = service.startup()

    if args.status:
        status = service.get_status(args.dataset)
        print(json.dumps(status.model_dump(), indent=2, ensure_ascii=False))
    else:
        print(f"\nRegistry startup complete:")
        for ds, state in changes.items():
            count = len(service.list_services(ds))
            print(f"  {ds}: {count} services, taxonomy={state.value}")

        total_status = service.get_status()
        print(f"\nTotal: {total_status.total_services} services across {len(total_status.datasets)} datasets")
        print(f"By source: {json.dumps(total_status.by_source)}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

DISPATCH = {
    "status":                _cmd_status,
    "datasets":              _cmd_datasets,
    "create-dataset":        _cmd_create_dataset,
    "delete-dataset":        _cmd_delete_dataset,
    "list":                  _cmd_list,
    "get":                   _cmd_get,
    "register-generic":      _cmd_register_generic,
    "register-a2a":          _cmd_register_a2a,
    "register-skill":        _cmd_register_skill,
    "update":                _cmd_update,
    "deregister":            _cmd_deregister,
    "deregister-skill":      _cmd_deregister_skill,
    "get-register-config":   _cmd_get_register_config,
    "set-register-config":   _cmd_set_register_config,
}


def main():
    if _is_legacy_invocation(sys.argv[1:]):
        return _handle_legacy(sys.argv[1:])

    parser = _build_parser()
    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    database_dir = Path(args.database_dir).resolve()
    config_path = Path(args.config) if args.config else None
    service = RegistryService(database_dir, config_path)
    service.startup()

    try:
        DISPATCH[args.command](service, args)
    except (ValueError, FileNotFoundError, KeyError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
