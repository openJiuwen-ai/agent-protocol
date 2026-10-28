#!/usr/bin/env bash
# CI entry point: bash scripts/build-sdk.sh <project> [project options...]
set -euo pipefail

usage() {
  echo "Usage: bash scripts/build-sdk.sh <project> [project options...]"
  echo "Projects: A4P-java-sdk, A4P-arkts-sdk, MCP/cpp-sdk, A2A/cpp-sdk"
  echo "ArkTS requires separately supplied SDK sources and scripts/build.sh."
}

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
  '') usage >&2; exit 2 ;;
  A4P-java-sdk) build_file="scripts/build.sh" ;;
  A4P-arkts-sdk) build_file="scripts/build.sh" ;;
  MCP/cpp-sdk) build_file="scripts/build.sh" ;;
  A2A/cpp-sdk) build_file="scripts/build.sh" ;;
  *) echo "Error: unknown build project '$1'." >&2; usage >&2; exit 2 ;;
esac
project="$1"
shift

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
LOG_FILE="${SCRIPT_DIR}/build.log"
exec > >(tee -a "${LOG_FILE}") 2>&1

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

project_dir="${ROOT_DIR}/${project}"
if [[ ! -f "${project_dir}/${build_file}" ]]; then
  log "[ERROR] ${project}: build script does not exist: ${project_dir}/${build_file}"
  exit 1
fi

log "[BUILD] ${project}"
if (
  cd -- "${project_dir}"
  if [[ "${project}" == A4P-java-sdk ]]; then
    bash "${build_file}" build "$@"
  else
    bash "${build_file}" "$@"
  fi
); then
  log "[OK] ${project}: build completed"
else
  status=$?
  log "[ERROR] ${project}: build failed (exit code ${status})"
  exit "${status}"
fi
