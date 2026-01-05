#!/bin/bash
set -e

echo "========================================="
echo "Building Prompt Example..."
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
echo "Running Prompt Example..."
echo "========================================="
./PromptExample

echo ""
echo "========================================="
echo "Prompt Example Completed!"
echo "========================================="
