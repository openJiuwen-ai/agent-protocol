#!/bin/bash
set -e

print_help() {
  echo "Usage: $0 [--port=<1-65535>]"
  echo ""
  echo "Examples:"
  echo "  $0"
  echo "  $0 --port=8000"
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
  else
    echo "Unknown argument: $arg"
    print_help
    exit 1
  fi
done

echo "========================================="
echo "Running All MCP Client Examples"
echo "========================================="
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "========================================="
echo "Example 1: Tool"
echo "========================================="
cd "$SCRIPT_DIR/tool_example"
chmod +x run_example.sh
./run_example.sh "${APP_ARGS[@]}"

echo ""
echo ""
echo "========================================="
echo "Example 2: Prompt"
echo "========================================="
cd "$SCRIPT_DIR/prompt_example"
chmod +x run_example.sh
./run_example.sh "${APP_ARGS[@]}"

echo ""
echo ""
echo "========================================="
echo "Example 3: Resource"
echo "========================================="
cd "$SCRIPT_DIR/resource_example"
chmod +x run_example.sh
./run_example.sh "${APP_ARGS[@]}"
