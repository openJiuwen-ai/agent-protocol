#!/bin/bash
set -e

echo "========================================="
echo "Building Server Example (Auth Mode)..."
echo "========================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR"

# Check if the main library is built
LIB_PATH="${SCRIPT_DIR}/../../output/libmcp.so"
if [ ! -f "$LIB_PATH" ]; then
    echo "Error: MCP library not found at $LIB_PATH"
    echo "Please build the main project first using: cd ../../ && ./scripts/build.sh"
    exit 1
fi

if [ ! "${RUN_ONLY:-false}" = true ]; then
    rm -rf build
    mkdir build
    cd build

    cmake "$SCRIPT_DIR"
    make

    echo "  ✓ Server example (auth mode) built successfully"
    echo ""
else
    if [ ! -d "build" ]; then
        echo "Error: Build directory not found"
        echo "Please run without --run-only first to build the example"
        exit 1
    fi
    cd build
fi

echo "========================================="
echo "Running Server Example (Auth Mode)..."
echo "========================================="
echo ""
echo "Server will run on http://127.0.0.1:8001/mcp"
echo ""
echo "Valid tokens for testing:"
echo "  - valid-token-12345 (scopes: read write) - will succeed"
echo "  - readonly-token-abcde (scopes: read) - will fail (403 Forbidden)"
echo ""
echo "Press Ctrl+C to stop the server"
echo ""

./ServerExample --auth

