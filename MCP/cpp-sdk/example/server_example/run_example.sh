#!/bin/bash
set -e

print_help() {
  echo "Usage: $0 [--port=<1-65535>] [--stateless] [--isJsonResponseDisable]"
  echo ""
  echo "Examples:"
  echo "  $0 --port=8000"
  echo "  $0 --stateless --port=8000"
  echo "  $0 --isJsonResponseDisable"
  echo "  $0 --port=8000 --isJsonResponseDisable"
}

SERVER_ARGS=()
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
    SERVER_ARGS+=("$arg")
  elif [ "$arg" = "--stateless" ]; then
    SERVER_ARGS+=("$arg")
  elif [ "$arg" = "--isJsonResponseDisable" ]; then
    SERVER_ARGS+=("$arg")
  else
    echo "Unknown argument: $arg"
    print_help
    exit 1
  fi
done

echo "========================================="
echo "Building Server Example..."
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
echo "Running Server Example..."
echo "========================================="

./ServerExample "${SERVER_ARGS[@]}"
