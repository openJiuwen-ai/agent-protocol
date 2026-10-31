#!/usr/bin/env bash
# Build the intelli_router wheel. Invoked by scripts/build-sdk.sh.
# Usage: bash build.sh [extra build args, e.g. --sdist]
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd -- "${PROJECT_DIR}"

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

if command -v uv >/dev/null 2>&1; then
  log "[BUILD] intelli_router: uv build --wheel $*"
  uv build --wheel "$@"
else
  log "[BUILD] intelli_router: python3 -m build --wheel (uv not found)"
  if ! python3 -m build --wheel "$@"; then
    log "[ERROR] intelli_router: build failed. Install uv, or run: python3 -m pip install build"
    exit 1
  fi
fi

log "[OK] intelli_router: artifacts in ${PROJECT_DIR}/dist:"
ls -1 "${PROJECT_DIR}/dist"
