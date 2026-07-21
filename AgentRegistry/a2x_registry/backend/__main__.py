"""Start the A2X Registry backend API server (standalone, no frontend).

Usage:
    a2x-registry                                 # API on http://127.0.0.1:8000
    a2x-registry --port 8080                     # Custom port
    python -m a2x_registry.backend               # equivalent module-form invocation

Auth admin subcommands (no server needed):
    a2x-registry auth init                       # bootstrap first admin key
    a2x-registry auth reset-admin --confirm      # rotate the bootstrap admin

Cluster subcommands (distributed sync):
    a2x-registry cluster init                    # generate node id (opt-in)
    a2x-registry cluster status                  # show sync state
"""

import argparse
import sys


def _serve(argv):
    """Original 'start server' path. Kept argv-explicit so the dispatcher
    below can hand it a slice without ``auth`` consuming the world.
    """
    parser = argparse.ArgumentParser(
        prog="a2x-registry",
        description="A2X Registry — backend API server",
    )
    parser.add_argument("--port", type=int, default=8000, help="Port (default: 8000)")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Host (default: 127.0.0.1)")
    parser.add_argument("--reload", action="store_true", default=False, help="Enable auto-reload")
    args = parser.parse_args(argv)

    print(f"\n  A2X Registry")
    print(f"  http://{args.host}:{args.port}")
    print(f"  Docs: http://{args.host}:{args.port}/docs\n")

    import uvicorn
    uvicorn.run(
        "a2x_registry.backend.app:app",
        host=args.host,
        port=args.port,
        reload=args.reload,
    )


def main():
    """Top-level dispatch: when ``argv[1] == 'auth'``, route to the auth
    CLI; otherwise run the original ``serve`` path. This keeps every
    pre-existing invocation (`a2x-registry`, `a2x-registry --port 8080`)
    byte-identical to before, while adding the new ``auth`` subcommand
    behind a single positional discriminator.
    """
    if len(sys.argv) >= 2 and sys.argv[1] == "auth":
        from a2x_registry.auth.cli import main as auth_main
        sys.exit(auth_main(sys.argv[2:]))
    if len(sys.argv) >= 2 and sys.argv[1] == "cluster":
        from a2x_registry.cluster.cli import main as cluster_main
        sys.exit(cluster_main(sys.argv[2:]))
    _serve(sys.argv[1:])


if __name__ == "__main__":
    main()
