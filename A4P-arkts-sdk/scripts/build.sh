#!/usr/bin/env bash
#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# Build the a4p HAR and its on-device test HAP from the command line,
# without opening DevEco Studio. Always cleans previous outputs first.
# The test HAP packaging step additionally requires java on PATH.
#
# Output: arkts/a4p/build/default/outputs/default/a4p.har
#         arkts/a4p/build/default/outputs/ohosTest/a4p-ohosTest-unsigned.hap

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
Usage: arkts/scripts/build.sh [--ohpm <path>] [--hvigorw <path>]

Builds the a4p HAR (release) and its on-device test HAP:
ohpm install, hvigor clean, assembleHar, genOnDeviceTestHap.
ohpm and hvigorw are resolved from PATH unless overridden.

  --ohpm <path>     ohpm executable, or the directory containing it
  --hvigorw <path>  hvigorw executable, or the directory containing it
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

  # build har package
  echo "==> hvigor assembleHar (release)"
  "${HVIGORW}" assembleHar --mode module \
    -p product=default \
    -p module="${MODULE_NAME}@default" \
    -p buildMode=release \
    --no-daemon

  # build test hap
  echo "==> hvigor genOnDeviceTestHap (test)"
  "${HVIGORW}" genOnDeviceTestHap --mode module \
    -p product=default \
    -p module="${MODULE_NAME}@ohosTest" \
    -p isOhosTest=true \
    -p buildMode=test \
    --analyze=normal \
    --parallel \
    --no-daemon
}

# --- Report the produced artifacts ---------------------------------------------
# Print the newest match of <pattern> under <project-relative dir>, or fail.
report_artifact() {
  local label="$1" reldir="$2" pattern="$3"
  local file
  file="$(ls -1t "${PROJECT_DIR}/${reldir}"/${pattern} 2>/dev/null | head -n 1 || true)"
  if [ -z "${file}" ]; then
    file="$(find "${PROJECT_DIR}/${MODULE_NAME}/build" -type f -name "${pattern}" -print -quit 2>/dev/null || true)"
  fi
  if [ -z "${file}" ]; then
    fail "no ${label} found under ${MODULE_NAME}/build"
  fi
  echo "==> ${label}: ${file} ($(du -h "${file}" | cut -f1))"
}

report() {
  # output of assembleHar (verified layout): build/default/outputs/default/
  report_artifact "HAR" "${MODULE_NAME}/build/default/outputs/default" '*.har'
  # output of genOnDeviceTestHap (verified layout): build/default/outputs/ohosTest/
  report_artifact "test HAP" "${MODULE_NAME}/build/default/outputs/ohosTest" '*.hap'
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
  # HAP packaging (genOnDeviceTestHap) spawns java; HAR packaging does not.
  command -v java >/dev/null 2>&1 || fail "java: not found on PATH.
The test HAP packaging step requires it (DevEco Studio's bundled JBR works)."
  echo "==> ohpm: ${OHPM}"
  echo "==> hvigorw: ${HVIGORW}"

  SECONDS=0
  build
  report
  echo "==> build succeeded in ${SECONDS}s"
}

trap 'status=$?; [ ${status} -eq 0 ] || printf "==> build FAILED after %ss (exit %s)\n" "${SECONDS}" "${status}" >&2' EXIT

main "$@"
