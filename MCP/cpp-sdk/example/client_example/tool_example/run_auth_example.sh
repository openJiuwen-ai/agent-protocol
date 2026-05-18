#!/bin/bash
set -e

echo "========================================="
echo "Building Tool Example (Auth Mode)..."
echo "========================================="

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd "$SCRIPT_DIR"

rm -rf build
mkdir build
cd build

cmake "$SCRIPT_DIR"
make

echo ""
echo "========================================="
echo "Running Tool Example (Auth Mode)..."
echo "========================================="
echo ""
echo "Expected server endpoint: http://127.0.0.1:8001/mcp"
echo ""
echo "Tokens used by this example:"
echo "  - valid-token-12345 (scopes: read write) - should succeed"
echo "  - readonly-token-abcde (scopes: read) - should fail (403 Forbidden)"
echo ""

./ToolExample --auth

echo ""
echo "========================================="
echo "Tool Auth Example Completed!"
echo "========================================="

