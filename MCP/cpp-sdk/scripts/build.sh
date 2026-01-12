#!/usr/bin/env bash

# Simple unified build entry for mcp_cpp.
#
# Features (current and planned):
#   - Choose build type: Debug / Release (default: Release)
#   - Build core library (libmcp / mcp.dll)
#   - Reserved switches for examples and unit tests
#
# Usage examples:
#   ./script/build.sh                    # Release build, core lib only
#   ./script/build.sh -t Debug           # Debug build
#   ./script/build.sh --with-tests       # build unit tests if CMakeLists adds them
#   ./script/build.sh --coverage         # build with code coverage (requires tests)
#

set -euo pipefail

# Default options
BUILD_TYPE="Release"
WITH_TESTS=0
WITH_COVERAGE=0
BUILD_DIR="build"
GENERATOR=""

MCP_BUILD_CLIENT=1
MCP_BUILD_SERVER=1
MCP_WITH_HTTP=1

print_help() {
  cat <<EOF
Usage: $0 [options]

Options:
  -t, --type <Debug|Release>   CMake build type (default: Release)
  -u, --with-tests             Build unit tests target(s) if available
  -c, --coverage               Enable code coverage (implies --with-tests)
  -b, --build-dir <dir>        Build directory (default: build)
  -g, --generator <name>       CMake generator (e.g. "Ninja", "NMake Makefiles")
  --no-client                  Do not build client components
  --no-server                  Do not build server components
  --no-http                    Do not build HTTP client transport components
  -h, --help                   Show this help message

Examples:
  $0
  $0 -t Debug
  $0 --type Release --with-tests
  $0 --coverage                # Build tests with coverage
  $0 --no-http --no-client     # No-HTTP Server
  $0 --no-http --no-server     # No-HTTP Client
  $0 --no-client               # HTTP Server
  $0 --no-server               # HTTP Client
  $0 --no-http                 # No-HTTP Server and Client
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--type)
      BUILD_TYPE="$2";
      shift 2;
      ;;
    -u|--with-tests)
      WITH_TESTS=1;
      shift;
      ;;
    -c|--coverage)
      WITH_COVERAGE=1;
      WITH_TESTS=1;
      shift;
      ;;
    -b|--build-dir)
      BUILD_DIR="$2";
      shift 2;
      ;;
    -g|--generator)
      GENERATOR="$2";
      shift 2;
      ;;
    --no-client)
      MCP_BUILD_CLIENT=0;
      shift;
      ;;
    --no-server)
      MCP_BUILD_SERVER=0;
      shift;
      ;;
    --no-http)
      MCP_WITH_HTTP=0;
      shift;
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
  Debug|Release|RelWithDebInfo|MinSizeRel)
    ;;
  *)
    echo "Invalid build type: ${BUILD_TYPE}. Use Debug, Release, RelWithDebInfo, or MinSizeRel." >&2
    exit 1
    ;;
esac

# Accurate line information for coverage is usually only available in debug mode.
if [[ ${WITH_COVERAGE} -eq 1 && "${BUILD_TYPE}" != "Debug" ]]; then
  echo "[INFO] Coverage is enabled, but build type is ${BUILD_TYPE}. For best results, consider using Debug mode (-t Debug)."
  echo "[INFO] Continuing with ${BUILD_TYPE}..."
fi

# Determine optimal job count for Linux (CPU cores + 1, but max 8 to avoid memory issues)
CPU_CORES=$(nproc)
OPTIMAL_JOBS=$((CPU_CORES + 1))
if [[ ${OPTIMAL_JOBS} -gt 8 ]]; then
  OPTIMAL_JOBS=8
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}/.."

if [[ "${BUILD_DIR}" = /* ]]; then
  BUILD_DIR_ABS="${BUILD_DIR}"
else
  BUILD_DIR_ABS="${SOURCE_DIR}/${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR_ABS}"

CMAKE_ARGS=("-S" "${SOURCE_DIR}" "-B" "${BUILD_DIR_ABS}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")

CMAKE_ARGS+=("-DMCP_BUILD_CLIENT=$([[ ${MCP_BUILD_CLIENT} -eq 1 ]] && echo ON || echo OFF)")
CMAKE_ARGS+=("-DMCP_BUILD_SERVER=$([[ ${MCP_BUILD_SERVER} -eq 1 ]] && echo ON || echo OFF)")
CMAKE_ARGS+=("-DMCP_WITH_HTTP=$([[ ${MCP_WITH_HTTP} -eq 1 ]] && echo ON || echo OFF)")

if [[ ${WITH_TESTS} -eq 1 ]]; then
  CMAKE_ARGS+=("-DMCP_ENABLE_TESTS=ON")
else
  CMAKE_ARGS+=("-DMCP_ENABLE_TESTS=OFF")
fi

if [[ ${WITH_COVERAGE} -eq 1 ]]; then
  CMAKE_ARGS+=("-DMCP_ENABLE_COVERAGE=ON")
else
  CMAKE_ARGS+=("-DMCP_ENABLE_COVERAGE=OFF")
fi

if [[ -n "${GENERATOR}" ]]; then
  cmake -G "${GENERATOR}" "${CMAKE_ARGS[@]}"
else
  cmake "${CMAKE_ARGS[@]}"
fi

echo "[INFO] Building with ${OPTIMAL_JOBS} parallel jobs (detected ${CPU_CORES} CPU cores)"
cmake --build "${BUILD_DIR_ABS}" -j${OPTIMAL_JOBS}

if [[ ${WITH_COVERAGE} -eq 1 ]]; then
  echo "[INFO] Coverage build finished."
  echo "[INFO] To run tests and generate coverage report, use:"
  echo "       make coverage        # For HTML report using gcovr"
  echo "  or   make coverage-simple # For simple gcov output"
  echo "  or   ctest -V             # To just run tests"
  echo ""
  echo "[INFO] Note: Coverage data (.gcda files) will be generated when tests run."
  echo "[INFO] Make sure you have gcovr installed for HTML reports: pip install gcovr"
fi

echo "[INFO] Build finished. Configuration: ${BUILD_TYPE}. Build dir: ${BUILD_DIR_ABS}"
