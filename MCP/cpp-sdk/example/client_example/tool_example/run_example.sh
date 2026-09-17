#!/bin/bash
set -e

print_help() {
  echo "Usage: $0 [--auth] [--port=<1-65535>]"
  echo ""
  echo "Examples:"
  echo "  $0"
  echo "  $0 --port=8000"
  echo "  $0 --auth"
  echo "  $0 --auth --port=8001"
}

APP_ARGS=()
for arg in "$@"; do
  if [ "$arg" = "--help" ] || [ "$arg" = "-h" ]; then
    print_help
    exit 0
  elif [[ "$arg" == --port=* ]]; then
    value="${arg#--port=}"
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
      echo "Invalid --port value: $value"
      print_help
      exit 1
    fi
    if [ "$value" -lt 1 ] || [ "$value" -gt 65535 ]; then
      echo "Invalid --port range: $value"
      print_help
      exit 1
    fi
    APP_ARGS+=("$arg")
  elif [ "$arg" = "--auth" ]; then
    APP_ARGS+=("$arg")
  else
    echo "Unknown argument: $arg"
    print_help
    exit 1
  fi
done

echo "========================================="
echo "Building Tool Example..."
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
echo "Running Tool Example..."
echo "========================================="

for arg in "${APP_ARGS[@]}"; do
  if [ "$arg" = "--auth" ]; then
    echo ""
    echo "Auth mode: connecting to http://127.0.0.1:8001/mcp (override with --port=)"
    echo "Using token: valid-token-12345"
    echo ""
    break
  fi
done

./ToolExample "${APP_ARGS[@]}"

echo ""
echo "========================================="
echo "Tool Example Completed!"
echo "========================================="
