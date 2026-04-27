# A2X Registry Demo UI

Source-only React + Vite web UI for the A2X Registry backend. **Not part of the pip package** — `pip install a2x-registry` ships the API only.

## Requirements

- Python ≥ 3.10 (with `a2x-registry` installed, e.g. via `pip install -e .` from the repo root)
- Node.js ≥ 18 (for the React frontend)

## Quick start

From the repo root:

```bash
python ui/launcher.py
```

The launcher auto-detects mode:

- If `ui/frontend/dist/` **does not exist** → starts Vite dev server on :5173 and backend on :8000.
  Open http://localhost:5173 (Vite proxies API calls to the backend).
- If `ui/frontend/dist/` **exists** → backend mounts the built static assets at `/`.
  Open http://localhost:8000.

First run auto-installs npm dependencies; subsequent runs skip that step.

## Building a production bundle

```bash
cd ui/frontend
npm install
npm run build
```

After this, `ui/launcher.py` (or `a2x-registry` with `A2X_FRONTEND_DIST_DIR` set) will serve the built assets in prod mode.

## Options

```bash
python ui/launcher.py --port 8080      # backend on :8080
python ui/launcher.py --no-frontend    # backend only (API only)
python ui/launcher.py --reload         # uvicorn auto-reload
```

## Why the split

The `a2x_registry/` Python package is what gets published to PyPI. Putting the React code under `ui/` keeps pip installs slim (no node_modules, no webpack output) while still giving source-clone users a one-command demo.
