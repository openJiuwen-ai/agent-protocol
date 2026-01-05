#!/usr/bin/env bash

# Run unit tests with code coverage and generate HTML reports
#
# This script will:
#   1. Check if build directory exists (with coverage enabled)
#   2. Run all unit tests and generate test result report
#   3. Generate HTML coverage report using gcovr
#
# Usage:
#   ./scripts/run_ut.sh [options]
#
# Options:
#   -o, --output-dir <dir>    Output directory for HTML reports (default: test_output)
#   -b, --build-dir <dir>     Build directory (default: build)
#   -h, --help                Show this help message

set -euo pipefail

# Default options
OUTPUT_DIR="test_output"
BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}/.."

print_help() {
  cat <<EOF
Usage: $0 [options]

Options:
  -o, --output-dir <dir>    Output directory for HTML report (default: test_output)
  -b, --build-dir <dir>     Build directory (default: build)
  -h, --help                Show this help message

Examples:
  $0
  $0 -o coverage_report
  $0 --output-dir my_coverage --build-dir build_debug
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -o|--output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    -b|--build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    -h|--help)
      print_help
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      print_help
      exit 1
      ;;
  esac
done

BUILD_DIR_ABS="${SOURCE_DIR}/${BUILD_DIR}"
OUTPUT_DIR_ABS="${SOURCE_DIR}/${OUTPUT_DIR}"

echo "========================================"
echo "  MCP C++ Unit Test with Coverage"
echo "========================================"
echo ""

# Step 1: Check if build directory exists
echo "[Step 1/3] Checking build directory..."
if [ ! -d "${BUILD_DIR_ABS}" ]; then
    echo "Error: Build directory not found: ${BUILD_DIR_ABS}"
    echo "Please build the project first using:"
    echo "  ./scripts/build.sh -t Debug --coverage"
    exit 1
fi
echo "  ✓ Build directory exists: ${BUILD_DIR_ABS}"
echo ""

# Step 2: Check and install dependencies
echo "[Step 2/4] Checking dependencies..."

# Check and install gcovr
if ! command -v gcovr >/dev/null 2>&1; then
    echo "  gcovr is not installed. Installing..."
    if command -v pip3 >/dev/null 2>&1; then
        pip3 install --user gcovr
    elif command -v pip >/dev/null 2>&1; then
        pip install --user gcovr
    else
        echo "Error: pip/pip3 is not installed"
        echo "Please install pip first or manually install gcovr"
        exit 1
    fi

    # Verify installation
    if ! command -v gcovr >/dev/null 2>&1; then
        echo "Error: Failed to install gcovr"
        echo "Please try manually: pip install gcovr"
        exit 1
    fi
fi
echo "  ✓ gcovr is installed ($(gcovr --version | head -n1))"

# Check and install junit2html for test report
if ! python3 -m pip show junit2html >/dev/null 2>&1; then
    echo "  pip show junit2html output:"
    python3 -m pip show junit2html 2>&1 || true
    echo "  junit2html is not installed. Installing..."
    if command -v pip3 >/dev/null 2>&1; then
        pip3 install --user junit2html
    elif command -v pip >/dev/null 2>&1; then
        pip install --user junit2html
    else
        echo "Warning: Cannot install junit2html, test report will not be generated"
    fi
fi
if python3 -m pip show junit2html >/dev/null 2>&1; then
    echo "  ✓ junit2html is installed"
fi
echo ""

# Remove old test output subdirectories if exists
echo "Preparing output directory..."
if [ -d "${OUTPUT_DIR_ABS}/ut" ]; then
    echo "  Removing old ut directory..."
    rm -rf "${OUTPUT_DIR_ABS}/ut"
fi
if [ -d "${OUTPUT_DIR_ABS}/coverage" ]; then
    echo "  Removing old coverage directory..."
    rm -rf "${OUTPUT_DIR_ABS}/coverage"
fi
mkdir -p "${OUTPUT_DIR_ABS}/ut"
mkdir -p "${OUTPUT_DIR_ABS}/coverage"
echo ""

# Step 3: Run tests and generate test report
echo "[Step 3/4] Running unit tests..."
cd "${BUILD_DIR_ABS}"

# Run tests with JUnit XML output (requires ctest 3.21+)
TEST_XML="${OUTPUT_DIR_ABS}/ut/ut_result.xml"
CTEST_VERSION=$(ctest --version 2>/dev/null | head -n1 | sed -E 's/.*version ([0-9]+\.[0-9]+).*/\1/')
CTEST_MAJOR=$(echo "${CTEST_VERSION}" | cut -d. -f1)
CTEST_MINOR=$(echo "${CTEST_VERSION}" | cut -d. -f2)

echo "  ctest version: ${CTEST_VERSION}"

# Check if tests exist first
echo "  Checking for available tests..."
if ! ctest -N 2>&1 | grep -q "Total Tests:"; then
    echo "  ⚠ Warning: No tests found in build directory"
    echo "  Please make sure tests are enabled and built"
fi

# Check if --output-junit is supported
if [ "${CTEST_MAJOR}" -gt 3 ] || ([ "${CTEST_MAJOR}" -eq 3 ] && [ "${CTEST_MINOR}" -ge 21 ]); then
    echo "  Running tests with --output-junit..."
    echo "  XML output: ${TEST_XML}"

    if ctest --output-on-failure --output-junit "${TEST_XML}"; then
        CTEST_EXIT_CODE=0
        echo "  ✓ All tests passed"
    else
        CTEST_EXIT_CODE=$?
        echo "  ⚠ Some tests failed (exit code: ${CTEST_EXIT_CODE})"
        echo "  Reports will still be generated"
    fi
else
    echo "  Note: ctest ${CTEST_VERSION} does not support --output-junit (requires 3.21+)"
    echo "  Running tests without XML generation..."

    if ctest --output-on-failure; then
        CTEST_EXIT_CODE=0
        echo "  ✓ All tests passed"
    else
        CTEST_EXIT_CODE=$?
        echo "  ⚠ Some tests failed"
    fi

    echo "  Skipping test report generation (requires ctest 3.21+)"
    echo ""
    echo "  To enable test reports, please upgrade ctest to version 3.21 or higher"
fi

# Debug: Check where XML file actually is
echo "  Searching for generated XML files..."
echo "  Looking in build directory: $(pwd)"
if [ -f "test_results.xml" ]; then
    echo "  ✓ Found test_results.xml in build directory"
    mv test_results.xml "${TEST_XML}"
    echo "  Moved to: ${TEST_XML}"
elif [ -f "${TEST_XML}" ]; then
    echo "  ✓ Found at expected location: ${TEST_XML}"
else
    echo "  ✗ XML file not found in either location"
    echo "  Files matching *xml in build dir:"
    find . -maxdepth 1 -name "*.xml" -ls 2>/dev/null || echo "    (none found)"
fi

echo "  Checking for test XML: ${TEST_XML}"
if [ -f "${TEST_XML}" ]; then
    echo "  ✓ XML file exists ($(wc -l < "${TEST_XML}") lines)"
else
    echo "  ✗ XML file not found at final location"
fi

# Generate HTML test report
if [ -f "${TEST_XML}" ]; then
    echo "  Generating test results HTML report..."
    echo "  Running: python3 -m junit2htmlreport ${TEST_XML} ${OUTPUT_DIR_ABS}/ut/ut_result.html"

    if python3 -m junit2htmlreport "${TEST_XML}" "${OUTPUT_DIR_ABS}/ut/ut_result.html" 2>&1; then
        if [ -f "${OUTPUT_DIR_ABS}/ut/ut_result.html" ]; then
            echo "  ✓ Test results report: ${OUTPUT_DIR_ABS}/ut/ut_result.html"
        else
            echo "  ✗ Command succeeded but HTML file not found"
        fi
    else
        echo "  ✗ Failed to generate HTML report"
        echo "  Checking junit2html installation..."
        if python3 -m pip show junit2html >/dev/null 2>&1; then
            echo "     junit2html is installed, but conversion failed"
            echo "     Try: pip3 install --user --upgrade junit2html"
        else
            echo "     junit2html is NOT installed"
            echo "     Installing now..."
            pip3 install --user junit2html
        fi
    fi
else
    echo "  ✗ Cannot generate HTML report: XML file missing"
fi

cd "${SOURCE_DIR}"
echo ""

echo ""
echo "========================================"
echo "  Reports Generated!"
echo "========================================"
echo ""
echo "Test Results Report:"
echo "  file://${OUTPUT_DIR_ABS}/ut/ut_result.html"
echo ""
