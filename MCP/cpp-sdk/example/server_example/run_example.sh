#!/bin/sh
set -e

print_help() {
  echo "Usage: $0 [--auth] [--port=<1-65535>] [--stateless] [--isJsonResponseDisable]"
  echo ""
  echo "Examples:"
  echo "  $0 --port=8000"
  echo "  $0 --auth"
  echo "  $0 --auth --port=8001"
  echo "  $0 --stateless --port=8000"
  echo "  $0 --isJsonResponseDisable"
  echo "  $0 --port=8000 --isJsonResponseDisable"
}

validate_port() {
  value=$1
  case "$value" in
    ''|*[!0-9]*)
      echo "Invalid --port value: $value"
      print_help
      exit 1
      ;;
  esac
  if [ "$value" -lt 1 ] || [ "$value" -gt 65535 ]; then
    echo "Invalid --port range: $value"
    print_help
    exit 1
  fi
}

SERVER_ARGS=""
AUTH_MODE=false

for arg in "$@"; do
  case "$arg" in
    -h|--help)
      print_help
      exit 0
      ;;
    --auth)
      SERVER_ARGS="${SERVER_ARGS} --auth"
      AUTH_MODE=true
      ;;
    --stateless)
      SERVER_ARGS="${SERVER_ARGS} --stateless"
      ;;
    --isJsonResponseDisable)
      SERVER_ARGS="${SERVER_ARGS} --isJsonResponseDisable"
      ;;
    --port=*)
      value=${arg#--port=}
      validate_port "$value"
      SERVER_ARGS="${SERVER_ARGS} ${arg}"
      ;;
    *)
      echo "Unknown argument: $arg"
      print_help
      exit 1
      ;;
  esac
done

echo "========================================="
echo "Building Server Example..."
echo "========================================="

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

cd "$SCRIPT_DIR"

rm -rf build
mkdir build
cd build

cmake "$SCRIPT_DIR"
make

echo ""
echo "========================================="
echo "Running Server Example..."
echo "========================================="

if [ "$AUTH_MODE" = true ]; then
  echo ""
  echo "Auth mode: endpoint defaults to http://127.0.0.1:8001/mcp (override with --port=)"
  echo "Valid token for testing: valid-token-12345 (scopes: read write)"
  echo ""
fi

# shellcheck disable=SC2086
exec ./ServerExample $SERVER_ARGS
