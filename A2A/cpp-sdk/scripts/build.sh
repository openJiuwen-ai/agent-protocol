#!/usr/bin/env bash

# Simple unified build entry for a2a.
#
# Features (current and planned):
#   - Choose build type: Debug / Release (default: Release)
#   - Build core library (liba2a / a2a.dll)
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
WITH_EXAMPLES=0
WITH_TESTS=0
WITH_COVERAGE=0
WITH_ASAN=0
BUILD_DIR="build"
GENERATOR=""
A2A_BUILD_CLIENT=1
A2A_BUILD_SERVER=1

print_help() {
  cat <<EOF
Usage: $0 [options]

Options:
  -t, --type <Debug|Release>   CMake build type (default: Release)
  -e, --with-examples          Build examples target(s)
  -u, --with-tests             Build unit tests target(s) if available
  -c, --coverage               Enable code coverage (implies --with-tests)
  -b, --build-dir <dir>        Build directory (default: build)
  -g, --generator <name>       CMake generator (e.g. "Ninja", "NMake Makefiles")
  --no-client                  Do not build client components
  --no-server                  Do not build server components
  --asan                       Enable AddressSanitizer (GCC/Clang), debug type will be used
  -h, --help                   Show this help message

Examples:
  $0
  $0 -t Debug
  $0 --type Release --with-examples --with-tests
  $0 --coverage                                 # Build tests with coverage
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--type)
      BUILD_TYPE="$2";
      shift 2;
      ;;
    -e|--with-examples)
      WITH_EXAMPLES=1;
      shift;
      ;;
    -u|--with-tests)
      WITH_TESTS=1;
      shift;
      ;;
    -c|--coverage)
      WITH_COVERAGE=1;
      WITH_TESTS=1;
      BUILD_TYPE="Debug";
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
    --asan)
      WITH_ASAN=1;
      BUILD_TYPE="Debug";
      shift;
      ;;
    --no-client)
      A2A_BUILD_CLIENT=0;
      shift;
      ;;
    --no-server)
      A2A_BUILD_SERVER=0;
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
  echo "[INFO] Coverage is enabled, but build type is ${BUILD_TYPE}. " \
    "For best results, consider using Debug mode (-t Debug)."
  echo "[INFO] Continuing with ${BUILD_TYPE}..."
fi

# Determine optimal job count (CPU cores + 1, but max 8 to avoid memory issues)
if command -v nproc >/dev/null 2>&1; then
  CPU_CORES=$(nproc)
else
  CPU_CORES=4
fi
OPTIMAL_JOBS=$((CPU_CORES + 1))
if [[ ${OPTIMAL_JOBS} -gt 8 ]]; then
  OPTIMAL_JOBS=8
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}/.."
BUILD_DIR_ABS="${SOURCE_DIR}/${BUILD_DIR}"

mkdir -p "${BUILD_DIR_ABS}"
cd "${BUILD_DIR_ABS}"

CMAKE_ARGS=("${SOURCE_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
CMAKE_ARGS+=("-DA2A_BUILD_SERVER=$([[ ${A2A_BUILD_SERVER} -eq 1 ]] && echo "ON" || echo "OFF")")
CMAKE_ARGS+=("-DA2A_BUILD_CLIENT=$([[ ${A2A_BUILD_CLIENT} -eq 1 ]] && echo "ON" || echo "OFF")")

if [[ ${WITH_EXAMPLES} -eq 1 ]]; then
  CMAKE_ARGS+=("-DA2A_ENABLE_EXAMPLES=ON")
else
  CMAKE_ARGS+=("-DA2A_ENABLE_EXAMPLES=OFF")
fi

if [[ ${WITH_TESTS} -eq 1 ]]; then
  CMAKE_ARGS+=("-DA2A_ENABLE_TESTS=ON")
else
  CMAKE_ARGS+=("-DA2A_ENABLE_TESTS=OFF")
fi

if [[ ${WITH_COVERAGE} -eq 1 ]]; then
  CMAKE_ARGS+=("-DA2A_ENABLE_COVERAGE=ON")
else
  CMAKE_ARGS+=("-DA2A_ENABLE_COVERAGE=OFF")
fi

# ASAN default build type is Debug
if [[ ${WITH_ASAN} -eq 1 ]]; then
  BUILD_TYPE="Debug"
  CMAKE_ARGS+=("-DASAN=enable")
fi

if [[ -n "${GENERATOR}" ]]; then
  cmake -G "${GENERATOR}" "${CMAKE_ARGS[@]}"
else
  cmake "${CMAKE_ARGS[@]}"
fi

echo "[INFO] Building with ${OPTIMAL_JOBS} parallel jobs (detected ${CPU_CORES} CPU cores)"
cmake --build . -j${OPTIMAL_JOBS}

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