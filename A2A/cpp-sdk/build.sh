#!/usr/bin/env bash

# Simple unified build entry for a2a_cpp.
#
# Features (current and planned):
#   - Choose build type: Debug / Release (default: Release)
#   - Build core library
#   - Reserved switches for examples and unit tests
#
# Usage examples:
#   ./build.sh                    # Release build, core lib only
#   ./build.sh -t Debug           # Debug build
#

set -euo pipefail

# Default options
BUILD_TYPE="Release"
BUILD_DIR="build"
GENERATOR=""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/scripts/install_deps.sh"

install_dependencies
check_dependencies

print_help() {
  cat <<EOF
Usage: $0 [options]

Options:
  -t, --type <Debug|Release>   CMake build type (default: Release)
  -b, --build-dir <dir>        Build directory (default: build)
  -h, --help                   Show this help message

Examples:
  $0
  $0 -t Debug
  $0 --type Release
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--type)
      BUILD_TYPE="$2";
      shift 2;
      ;;
    -b|--build-dir)
      BUILD_DIR="$2";
      shift 2;
      ;;
    -h|--help)
      print_help;
      exit 0;
      ;;
    *)
      echo "Unknown option: $1" >&2;
      print_help;
      exit 1;
      ;;
  esac
done

# Normalize build type
case "${BUILD_TYPE}" in
  Debug|Release)
    ;;
  *)
    echo "Invalid build type: ${BUILD_TYPE}. Use Debug or Release." >&2
    exit 1
    ;;
esac

# Determine optimal job count for Linux (CPU cores + 1, but max 8 to avoid memory issues)
CPU_CORES=$(nproc)
OPTIMAL_JOBS=$((CPU_CORES + 1))
if [[ ${OPTIMAL_JOBS} -gt 8 ]]; then
  OPTIMAL_JOBS=8
fi

SOURCE_DIR="${SCRIPT_DIR}"
BUILD_DIR_ABS="${SOURCE_DIR}/${BUILD_DIR}"

mkdir -p "${BUILD_DIR_ABS}"
cd "${BUILD_DIR_ABS}"

CMAKE_ARGS=("${SOURCE_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")

cmake "${CMAKE_ARGS[@]}"

echo "[INFO] Building with ${OPTIMAL_JOBS} parallel jobs (detected ${CPU_CORES} CPU cores)"
cmake --build . -j${OPTIMAL_JOBS}

echo "[INFO] Build finished. Configuration: ${BUILD_TYPE}. Build dir: ${BUILD_DIR_ABS}"