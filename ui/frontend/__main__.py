"""Build the frontend for production.

Usage:
    python -m src.frontend   # clean dist/ (if exists), install deps (if needed), build
"""

import shutil
import subprocess
import sys
from pathlib import Path

FRONTEND_DIR = Path(__file__).parent


def main():
    dist = FRONTEND_DIR / "dist"
    if dist.exists():
        shutil.rmtree(dist)
        print("  Cleaned dist/")

    if not (FRONTEND_DIR / "node_modules").exists():
        print("  Installing dependencies...")
        r = subprocess.run("npm install", cwd=str(FRONTEND_DIR), shell=True)
        if r.returncode != 0:
            print("  ERROR: npm install failed.")
            sys.exit(1)

    print("  Building...")
    r = subprocess.run("npm run build", cwd=str(FRONTEND_DIR), shell=True)
    if r.returncode != 0:
        print("  ERROR: build failed.")
        sys.exit(1)

    print("  Done. Run `python -m src.ui` to start.")


if __name__ == "__main__":
    main()

