#!/usr/bin/env bash
#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# Build the a4p HAR from the command line, without opening DevEco Studio.
# Always cleans previous outputs first.
#
# Output: arkts/a4p/build/default/outputs/default/a4p.har

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODULE_NAME="a4p"

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: arkts/scripts/build.sh [--ohpm <path>] [--hvigorw <path>] [--sdk <path>]

Builds the a4p HAR in release mode: ohpm install, hvigor clean, assembleHar.
ohpm and hvigorw are resolved from PATH unless overridden.

  --ohpm <path>     ohpm executable, or the directory containing it
  --hvigorw <path>  hvigorw executable, or the directory containing it
  --sdk <path>      SDK root directory, exported as DEVECO_SDK_HOME for hvigor
                    (default: the value already in the environment)
  -h, --help        show this help

Example:
  arkts/scripts/build.sh --ohpm /path/to/ohpm/bin --hvigorw /path/to/hvigor/bin
EOF
}

# --- Resolve the ohpm / hvigorw executables ------------------------------------
# A --ohpm/--hvigorw value may be the executable itself or its directory.
resolve_tool() {
  local value="$1" name="$2"
  if [ -d "${value}" ]; then
    value="${value%/}/${name}"
  fi
  printf '%s\n' "${value}"
}

check_tool() {
  local tool="$1" name="$2"
  if [ ! -x "${tool}" ] && ! command -v "${tool}" >/dev/null 2>&1; then
    fail "${name}: not found or not executable: ${tool}
Pass --${name} <path> or add ${name} to PATH."
  fi
}

# --- Build ---------------------------------------------------------------------
build() {
  cd "${PROJECT_DIR}"

  echo "==> ohpm install"
  "${OHPM}" install --all

  echo "==> hvigor clean"
  "${HVIGORW}" clean --no-daemon

  echo "==> hvigor assembleHar (release)"
  "${HVIGORW}" assembleHar --mode module \
    -p product=default \
    -p module="${MODULE_NAME}@default" \
    -p buildMode=release \
    --no-daemon
}

# --- Report the produced artifact ----------------------------------------------
report() {
  local har_file
  # output of assembleHar (verified layout): build/default/outputs/default/
  har_file="$(ls -1t "${PROJECT_DIR}/${MODULE_NAME}"/build/default/outputs/default/*.har 2>/dev/null | head -n 1 || true)"
  if [ -z "${har_file}" ]; then
    har_file="$(find "${PROJECT_DIR}/${MODULE_NAME}/build" -type f -name '*.har' -print -quit 2>/dev/null || true)"
  fi
  if [ -z "${har_file}" ]; then
    fail "build reported success but no .har found under ${MODULE_NAME}/build"
  fi
  echo "==> HAR: ${har_file} ($(du -h "${har_file}" | cut -f1))"
}

main() {
  OHPM="ohpm"
  HVIGORW="hvigorw"
  while [ $# -gt 0 ]; do
    case "$1" in
      --ohpm)
        [ $# -ge 2 ] || fail "--ohpm requires a value"
        OHPM="$(resolve_tool "$2" ohpm)"
        shift 2
        ;;
      --ohpm=*)
        OHPM="$(resolve_tool "${1#--ohpm=}" ohpm)"
        shift
        ;;
      --hvigorw)
        [ $# -ge 2 ] || fail "--hvigorw requires a value"
        HVIGORW="$(resolve_tool "$2" hvigorw)"
        shift 2
        ;;
      --hvigorw=*)
        HVIGORW="$(resolve_tool "${1#--hvigorw=}" hvigorw)"
        shift
        ;;
      --sdk)
        [ $# -ge 2 ] || fail "--sdk requires a value"
        [ -d "$2" ] || fail "--sdk: not a directory: $2"
        export DEVECO_SDK_HOME="$2"
        shift 2
        ;;
      --sdk=*)
        [ -d "${1#--sdk=}" ] || fail "--sdk: not a directory: ${1#--sdk=}"
        export DEVECO_SDK_HOME="${1#--sdk=}"
        shift
        ;;
      -h | --help)
        usage
        exit 0
        ;;
      *)
        usage >&2
        fail "unknown argument: $1"
        ;;
    esac
  done

  check_tool "${OHPM}" ohpm
  check_tool "${HVIGORW}" hvigorw
  echo "==> ohpm: ${OHPM}"
  echo "==> hvigorw: ${HVIGORW}"
  if [ -n "${DEVECO_SDK_HOME:-}" ]; then
    echo "==> sdk: ${DEVECO_SDK_HOME}"
  else
    echo "==> NOTE: DEVECO_SDK_HOME not set (pass --sdk <path> if hvigor complains)" >&2
  fi

  SECONDS=0
  build
  report
  echo "==> build succeeded in ${SECONDS}s"
}

trap 'status=$?; [ ${status} -eq 0 ] || printf "==> build FAILED after %ss (exit %s)\n" "${SECONDS}" "${status}" >&2' EXIT

main "$@"
