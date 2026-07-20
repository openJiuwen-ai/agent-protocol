"""Launch the A2X Registry demo UI (backend + React frontend).

This launcher is **only** shipped with the source clone — it is not part of
the ``a2x-registry`` pip package. Users who ``pip install a2x-registry`` get
the API server only (``a2x-registry``); the demo web UI requires cloning the
repo and having Node.js installed.

Two modes (auto-selected from whether ``ui/frontend/dist/`` exists):

- **Dev mode**  — no ``dist/``: starts ``npm run dev`` on :5173 **and**
  the backend on :8000. Open http://localhost:5173.
- **Prod mode** — ``dist/`` present: the backend mounts the built assets
  via ``StaticFiles`` (env-var ``A2X_FRONTEND_DIST_DIR`` is set here and
  read by :mod:`a2x_registry.backend.app`). Open http://localhost:8000.

Usage::

    python ui/launcher.py                 # auto mode, default ports
    python ui/launcher.py --port 8080     # custom backend port
    python ui/launcher.py --no-frontend   # backend only
    python ui/launcher.py --reload        # enable uvicorn auto-reload

To build the frontend for prod mode::

    cd ui/frontend && npm install && npm run build
"""

import argparse
import os
import signal
import socket
import subprocess
import sys
from pathlib import Path

FRONTEND_DIR = Path(__file__).parent / "frontend"


def _get_local_ip() -> str | None:
    """Return the machine's LAN IP address, or None if unavailable."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return None


def _kill_proc_tree(proc: subprocess.Popen) -> None:
    """Kill a process and its entire tree (Windows-safe)."""
    if proc.poll() is not None:
        return
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/T", "/F", "/PID", str(proc.pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    else:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except (ProcessLookupError, OSError):
            proc.kill()


def _start_frontend_dev(backend_port: int) -> subprocess.Popen | None:
    """Start Vite dev server as a child process. ``None`` when serving from ``dist/``."""
    if (FRONTEND_DIR / "dist").exists():
        print(f"  Frontend: http://127.0.0.1:{backend_port}  (serving from dist/)")
        return None

    if not (FRONTEND_DIR / "node_modules").exists():
        print("  [info] Installing frontend dependencies (first time)...")
        subprocess.run(
            "npm install",
            cwd=str(FRONTEND_DIR),
            shell=True,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    return subprocess.Popen(
        "npm run dev -- --clearScreen false",
        cwd=str(FRONTEND_DIR),
        shell=True,
        stdout=sys.stdout,
        stderr=sys.stderr,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="A2X Registry Demo UI (backend + frontend)")
    parser.add_argument("--port", type=int, default=8000, help="Backend port (default: 8000)")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Host to bind (default: 0.0.0.0)")
    parser.add_argument("--no-frontend", action="store_true", help="Skip starting the frontend")
    parser.add_argument("--reload", action="store_true", default=False, help="Enable uvicorn auto-reload")
    args = parser.parse_args()

    # Prod mode: tell backend to serve the built static assets.
    dist_dir = FRONTEND_DIR / "dist"
    if dist_dir.exists():
        os.environ["A2X_FRONTEND_DIST_DIR"] = str(dist_dir)

    lan_ip = _get_local_ip()
    display_host = lan_ip if args.host in ("0.0.0.0", "::") else args.host

    print(f"\n  A2X Registry Demo UI")
    print(f"  Local:    http://localhost:{args.port}")
    if display_host and display_host not in ("localhost", "127.0.0.1"):
        print(f"  Network:  http://{display_host}:{args.port}")

    vite_proc = None
    if not args.no_frontend:
        vite_proc = _start_frontend_dev(args.port)
        if vite_proc:
            print(f"  Frontend: http://localhost:5173  (Vite dev server)")
            if display_host and display_host not in ("localhost", "127.0.0.1"):
                print(f"  Frontend: http://{display_host}:5173  (remote access)")
            print()
        else:
            print()
    else:
        print()

    import uvicorn

    try:
        uvicorn.run(
            "a2x_registry.backend.app:app",
            host=args.host,
            port=args.port,
            reload=args.reload,
        )
    finally:
        if vite_proc:
            _kill_proc_tree(vite_proc)


if __name__ == "__main__":
    main()
