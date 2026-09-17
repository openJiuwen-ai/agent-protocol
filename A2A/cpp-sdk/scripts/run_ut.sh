#!/usr/bin/env bash
# Build and run unit tests (CTest + optional gcovr coverage).
#
# Usage:
#   bash ./scripts/run_ut.sh              # build tests + run ctest
#   bash ./scripts/run_ut.sh --no-coverage  # skip -c (faster, no gcovr)
#   A2A_SKIP_ASAN=1 bash ./scripts/run_ut.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${A2A_BUILD_DIR:-build}"

WITH_COVERAGE=1
WITH_ASAN=1
for arg in "$@"; do
    case "$arg" in
        --no-coverage) WITH_COVERAGE=0 ;;
        --no-asan) WITH_ASAN=0 ;;
        -h|--help)
            echo "Usage: $0 [--no-coverage] [--no-asan]"
            exit 0
            ;;
    esac
done

echo "==================== STEP 1: build ========================="
BUILD_ARGS=(-u -t Debug -b "${BUILD_DIR}")
if [[ ${WITH_COVERAGE} -eq 1 ]]; then
    BUILD_ARGS+=(-c)
fi
if [[ ${WITH_ASAN} -eq 1 && -z "${A2A_SKIP_ASAN:-}" ]]; then
    BUILD_ARGS+=(--asan)
fi
bash "${SCRIPT_DIR}/build.sh" "${BUILD_ARGS[@]}"

echo "==================== STEP 2: run ut test ========================="
cd "${ROOT}/${BUILD_DIR}"

# Resolve lib paths for runtime (liba2a + fetched curl if present)
CTEST_LD_LIBRARY_PATH="${ROOT}/output/lib:${ROOT}/${BUILD_DIR}"
for _libdir in \
    "${ROOT}/third_party/curl-build/lib" \
    "${ROOT}/_deps/curl-build/lib" \
    "${ROOT}/${BUILD_DIR}/third_party/curl-build/lib"; do
    if [[ -d "${_libdir}" ]]; then
        CTEST_LD_LIBRARY_PATH="${_libdir}:${CTEST_LD_LIBRARY_PATH}"
    fi
done

RUN_ENV=(env "LD_LIBRARY_PATH=${CTEST_LD_LIBRARY_PATH}:${LD_LIBRARY_PATH:-}")

if [[ ${WITH_ASAN} -eq 1 && -z "${A2A_SKIP_ASAN:-}" ]]; then
    ASAN_LIB="$(gcc -print-file-name=libasan.so 2>/dev/null || true)"
    if [[ -n "${ASAN_LIB}" && -f "${ASAN_LIB}" ]]; then
        RUN_ENV+=(LD_PRELOAD="${ASAN_LIB}" ASAN_OPTIONS="${ASAN_OPTIONS:-verify_asan_link_order=0}")
    fi
fi

echo "Running ctest from ${ROOT}/${BUILD_DIR} ..."
"${RUN_ENV[@]}" ctest -V --output-on-failure

if [[ ${WITH_COVERAGE} -eq 0 ]]; then
    echo "==================== DONE (tests only) ===================="
    exit 0
fi

if ! command -v gcovr >/dev/null 2>&1; then
    echo "[WARN] gcovr not found; skipping coverage report. Install with: pip install gcovr"
    echo "==================== DONE ================================="
    exit 0
fi

echo "==================== STEP 3: coverage summary ================="
gcovr \
    --root "${ROOT}" \
    --filter '.*src/.*' \
    --exclude '.*third_party/.*' \
    --print-summary | tail -n 4

echo "==================== STEP 4: generate html report ================="
gcovr \
    --root "${ROOT}" \
    --filter '.*src/.*' \
    --exclude '.*third_party/.*' \
    --html --html-details \
    -o coverage_report.html

echo "覆盖率报告: ${ROOT}/${BUILD_DIR}/coverage_report.html"
echo "==================== DONE ================================="
