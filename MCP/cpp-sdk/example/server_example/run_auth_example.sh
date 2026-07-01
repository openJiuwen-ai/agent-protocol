#!/bin/sh
set -e

print_help() {
  echo "Usage: $0 [-h|--help]"
  echo ""
  echo "Build and run ServerExample in auth mode (http://127.0.0.1:8001/mcp)."
  echo ""
  echo "Equivalent to: ./run_example.sh --auth"
  echo ""
  echo "Valid tokens for testing:"
  echo "  - valid-token-12345 (scopes: read write) - will succeed"
  echo "  - readonly-token-abcde (scopes: read) - will fail (403 Forbidden)"
}

for arg in "$@"; do
  case "$arg" in
    -h|--help)
      print_help
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg"
      print_help
      exit 1
      ;;
  esac
done

echo "========================================="
echo "Building Server Example (Auth Mode)..."
echo "========================================="

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

cd "$SCRIPT_DIR"

LIB_PATH="${SCRIPT_DIR}/../../output/lib/libmcp.so"
if [ ! -f "$LIB_PATH" ]; then
  echo "Error: MCP library not found at $LIB_PATH"
  echo "Please build the main project first using: cd ../../ && bash scripts/build.sh"
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
    echo "Please run without RUN_ONLY=true first to build the example"
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

exec ./ServerExample --auth
