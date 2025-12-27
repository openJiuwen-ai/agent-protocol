#!/bin/bash
set -e

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
./run_example.sh

echo ""
echo ""
echo "========================================="
echo "Example 2: Prompt"
echo "========================================="
cd "$SCRIPT_DIR/prompt_example"
chmod +x run_example.sh
./run_example.sh

echo ""
echo ""
echo "========================================="
echo "Example 3: Resource"
echo "========================================="
cd "$SCRIPT_DIR/resource_example"
chmod +x run_example.sh
./run_example.sh

