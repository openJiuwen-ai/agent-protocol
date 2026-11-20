#!/usr/bin/env bash
# CI entry point: bash build/run-ut.sh <project> [project options...]
set -euo pipefail

usage() {
  echo "Usage: bash build/run-ut.sh <project> [project options...]"
  echo "Projects: A4P-java-sdk, MCP/cpp-sdk"
}

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
  '') usage >&2; exit 2 ;;
  A4P-java-sdk) test_file="scripts/ut.sh" ;;
  MCP/cpp-sdk) test_file="scripts/run_ut.sh" ;;
  *) echo "Error: unsupported UT project '$1'." >&2; usage >&2; exit 2 ;;
esac
project="$1"
shift

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
LOG_FILE="${SCRIPT_DIR}/ut.log"
exec > >(tee -a "${LOG_FILE}") 2>&1

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

project_dir="${ROOT_DIR}/${project}"
test_dir="${project_dir}/$(dirname "${test_file}")"
if [[ ! -d "${test_dir}" ]]; then
  log "[SKIP] ${project}: UT script directory does not exist: ${test_dir}"
  exit 0
fi
if [[ ! -f "${project_dir}/${test_file}" ]]; then
  log "[SKIP] ${project}: UT script does not exist: ${project_dir}/${test_file}"
  exit 0
fi

log "[UT] ${project}"
# Java ut.sh resolves its project and reports relative to WORKSPACE.
if (cd -- "${project_dir}" && WORKSPACE="${ROOT_DIR}" bash "${test_file}" "$@"); then
  log "[OK] ${project}: UT completed"
else
  status=$?
  log "[ERROR] ${project}: UT failed (exit code ${status})"
  exit "${status}"
fi
