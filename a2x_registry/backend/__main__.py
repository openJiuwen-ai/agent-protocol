"""Start the A2X backend API server (standalone, no frontend).

Usage:
    python -m a2x_registry.backend              # API on http://127.0.0.1:8000
    python -m a2x_registry.backend --port 8080  # Custom port
"""

import argparse


def main():
    parser = argparse.ArgumentParser(description="A2X Backend API Server")
    parser.add_argument("--port", type=int, default=8000, help="Port (default: 8000)")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Host (default: 127.0.0.1)")
    parser.add_argument("--reload", action="store_true", default=False, help="Enable auto-reload")
    args = parser.parse_args()

    print(f"\n  A2X Backend API")
    print(f"  http://{args.host}:{args.port}")
    print(f"  Docs: http://{args.host}:{args.port}/docs\n")

    import uvicorn
    uvicorn.run(
        "a2x_registry.backend.app:app",
        host=args.host,
        port=args.port,
        reload=args.reload,
    )


if __name__ == "__main__":
    main()
