#!/bin/sh

# MCP C++ Example Runner Script
# This script builds and runs MCP examples

set -e
if (set -o pipefail) 2>/dev/null; then
    set -o pipefail
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$(dirname "${SCRIPT_DIR}")"
EXAMPLE_DIR="${SOURCE_DIR}/example"
MASTER_LOG="${SOURCE_DIR}/run_example.log"
TEE_PID=""
LOG_FIFO=""

setup_master_log() {
    : > "${MASTER_LOG}"

    if ! [ -t 1 ]; then
        exec >> "${MASTER_LOG}" 2>&1
        return
    fi

    if command -v mkfifo >/dev/null 2>&1 && command -v tee >/dev/null 2>&1; then
        if command -v mktemp >/dev/null 2>&1; then
            LOG_FIFO=$(mktemp -u 2>/dev/null || mktemp -u -t run_example)
        else
            LOG_FIFO="/tmp/run_example_fifo.$$"
        fi
        rm -f "${LOG_FIFO}"
        mkfifo "${LOG_FIFO}"
        tee -a "${MASTER_LOG}" < "${LOG_FIFO}" &
        TEE_PID=$!
        exec > "${LOG_FIFO}" 2>&1
        return
    fi

    exec >> "${MASTER_LOG}" 2>&1
}

setup_master_log
echo "Overall log: ${MASTER_LOG}"

# Default options
EXAMPLE_TYPE="all"
BUILD_ONLY=false
RUN_ONLY=false
SERVER_PID=""
SERVER_PORT="${SERVER_PORT:-8000}"
FORCE_KILL_PORT=false

# Cleanup function to kill background server and log tee
cleanup() {
    if [ -n "${SERVER_PID}" ] && ps -p "${SERVER_PID}" > /dev/null 2>&1; then
        echo ""
        echo "Stopping background server (PID: ${SERVER_PID})..."
        kill "${SERVER_PID}"
        wait "${SERVER_PID}" 2>/dev/null
        echo "  ✓ Server stopped"
    fi

    if [ -n "${TEE_PID}" ]; then
        # Close fifo write ends (stdout and stderr) so background tee receives EOF.
        if [ -n "${LOG_FIFO}" ]; then
            exec 1>&- 2>&-
        fi
        wait "${TEE_PID}" 2>/dev/null || true
        TEE_PID=""
    fi
    if [ -n "${LOG_FIFO}" ] && [ -e "${LOG_FIFO}" ]; then
        rm -f "${LOG_FIFO}"
        LOG_FIFO=""
    fi
}

get_listening_pids_by_port() {
    local port="$1"

    if command -v lsof > /dev/null 2>&1; then
        lsof -tiTCP:"${port}" -sTCP:LISTEN 2>/dev/null | sort -u
        return 0
    fi

    if command -v ss > /dev/null 2>&1; then
        ss -ltnp "sport = :${port}" 2>/dev/null \
            | sed -n 's/.*pid=\([0-9][0-9]*\),.*/\1/p' \
            | sort -u
        return 0
    fi

    return 1
}

free_port_if_occupied() {
    local port="$1"
    local pids

    pids="$(get_listening_pids_by_port "${port}" || true)"
    if [ -z "${pids}" ]; then
        return 0
    fi

    if [ "${FORCE_KILL_PORT}" != true ]; then
        echo ""
        echo "Error: Port ${port} is already in use. Listener PID(s):"
        echo "${pids}" | sed 's/^/  - PID: /'
        echo ""
        echo "Use '--force-kill-port' to terminate them automatically, or choose another port with '--port <PORT>' (or env SERVER_PORT)."
        exit 1
    fi

    echo ""
    echo "Port ${port} is already in use. Stopping listener process(es):"
    echo "${pids}" | sed 's/^/  - PID: /'

    echo "${pids}" | xargs kill 2>/dev/null || true
    sleep 2

    pids="$(get_listening_pids_by_port "${port}" || true)"
    if [ -n "${pids}" ]; then
        echo "Port ${port} is still in use after SIGTERM. Forcing kill (SIGKILL):"
        echo "${pids}" | sed 's/^/  - PID: /'
        echo "${pids}" | xargs kill -9 2>/dev/null || true
        sleep 1
    fi

    pids="$(get_listening_pids_by_port "${port}" || true)"
    if [ -n "${pids}" ]; then
        echo "Error: Failed to free port ${port}. Remaining PID(s):"
        echo "${pids}" | sed 's/^/  - PID: /'
        exit 1
    fi
}

# Register cleanup function to run on exit
trap cleanup EXIT INT TERM

# Parse command line arguments
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
  -t, --type <TYPE>     Specify example type to run:
                          all     - Run all examples (default)
                          server  - Run server example only
                          client  - Run all client examples
                          tool    - Run tool client example
                          prompt  - Run prompt client example
                          resource - Run resource client example
                          sampling - Run sampling client example (server-to-client sampling)
  -p, --port <PORT>     Server listen port (default: 8000, or env SERVER_PORT)
  -f, --force-kill-port If the server port is in use, kill the process(es) occupying it
  -b, --build-only      Only build examples, do not run
  -r, --run-only        Only run examples (skip build)
  -h, --help            Show this help message

Examples:
  $0                    # Run all examples
  $0 -t server          # Run server example only
  $0 -t client          # Run all client examples
  $0 -t tool            # Run tool client example
  $0 -t sampling        # Run sampling client example
  $0 --build-only       # Build all examples without running

EOF
    exit 0
}

while [ $# -gt 0 ]; do
    case $1 in
        -t|--type)
            EXAMPLE_TYPE="$2"
            shift 2
            ;;
        -p|--port)
            SERVER_PORT="$2"
            shift 2
            ;;
        -f|--force-kill-port)
            FORCE_KILL_PORT=true
            shift
            ;;
        -b|--build-only)
            BUILD_ONLY=true
            shift
            ;;
        -r|--run-only)
            RUN_ONLY=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Validate example type
case "${EXAMPLE_TYPE}" in
    all|server|client|tool|prompt|resource|sampling)
        ;;
    *)
        echo "Error: Invalid example type '${EXAMPLE_TYPE}'"
        echo "Valid types: all, server, client, tool, prompt, resource, sampling"
        exit 1
        ;;
esac

echo "========================================"
echo "  MCP C++ Example Runner"
echo "========================================"
echo ""
echo "Example Type: ${EXAMPLE_TYPE}"
echo "Build Only:   ${BUILD_ONLY}"
echo "Run Only:     ${RUN_ONLY}"
echo ""

# Function to build an example
build_example() {
    local example_path=$1
    local example_name=$2

    echo "========================================="
    echo "Building ${example_name}..."
    echo "========================================="

    cd "${example_path}"

    if [ ! "${RUN_ONLY}" = true ]; then
        rm -rf build
        mkdir build
        cd build

        cmake ..
        make

        echo "  ✓ ${example_name} built successfully"
    else
        if [ ! -d "build" ]; then
            echo "Error: Build directory not found for ${example_name}"
            echo "Please run without --run-only first to build the example"
            exit 1
        fi
        cd build
    fi

    echo ""
}

# Function to run an example
run_example() {
    local example_path=$1
    local example_name=$2
    local executable=$3
    local log_file=$4

    if [ "${BUILD_ONLY}" = true ]; then
        return
    fi

    echo "========================================="
    echo "Running ${example_name}..."
    echo "========================================="

    cd "${example_path}/build"

    if [ ! -f "${executable}" ]; then
        echo "Error: Executable '${executable}' not found"
        exit 1
    fi

    if [ -z "${log_file}" ]; then
        log_file="${example_path}/build/${executable}_output.log"
    fi

    echo "  Output will be saved to: ${log_file}"

    # Run example and capture output
    ./${executable} | tee "${log_file}"

    echo ""
    echo "  ${example_name} completed"
    echo ""
}

# Function to run server example (in background)
run_server_example() {
    echo ""
    echo "========================================"
    echo "  Server Example"
    echo "========================================"
    echo ""

    build_example "${EXAMPLE_DIR}/server_example" "Server Example"

    if [ "${BUILD_ONLY}" = true ]; then
        return
    fi

    echo "========================================="
    echo "Running Server Example (in background)..."
    echo "========================================="

    cd "${EXAMPLE_DIR}/server_example/build"

    if [ ! -f "ServerExample" ]; then
        echo "Error: Executable 'ServerExample' not found"
        exit 1
    fi

    free_port_if_occupied "${SERVER_PORT}"

    # Run server in background
    ./ServerExample > server_example.log 2>&1 &
    SERVER_PID=$!

    echo "  Server started with PID: ${SERVER_PID}"
    echo "  Log file: ${EXAMPLE_DIR}/server_example/build/server_example.log"
    echo "  Waiting for server to initialize..."

    # Wait a moment for server to start
    sleep 2

    # Check if server is still running
    if ps -p "${SERVER_PID}" > /dev/null 2>&1; then
        echo "  Server is running"
    else
        echo "  Server failed to start. Check log file:"
        cat server_example.log
        exit 1
    fi

    echo ""
    echo "  Note: Server is running in background. Use 'kill ${SERVER_PID}' to stop it."
    echo ""
}

# Function to run client tool example
run_tool_example() {
    echo ""
    echo "========================================"
    echo "  Tool Client Example"
    echo "========================================"
    echo ""

    build_example "${EXAMPLE_DIR}/client_example/tool_example" "Tool Example"
    run_example "${EXAMPLE_DIR}/client_example/tool_example" "Tool Example" "ToolExample" "${EXAMPLE_DIR}/client_example/tool_example/build/tool_example_output.log"
}

# Function to run client prompt example
run_prompt_example() {
    echo ""
    echo "========================================"
    echo "  Prompt Client Example"
    echo "========================================"
    echo ""

    build_example "${EXAMPLE_DIR}/client_example/prompt_example" "Prompt Example"
    run_example "${EXAMPLE_DIR}/client_example/prompt_example" "Prompt Example" "PromptExample" "${EXAMPLE_DIR}/client_example/prompt_example/build/prompt_example_output.log"
}

# Function to run client resource example
run_resource_example() {
    echo ""
    echo "========================================"
    echo "  Resource Client Example"
    echo "========================================"
    echo ""

    build_example "${EXAMPLE_DIR}/client_example/resource_example" "Resource Example"
    run_example "${EXAMPLE_DIR}/client_example/resource_example" "Resource Example" "ResourceExample" "${EXAMPLE_DIR}/client_example/resource_example/build/resource_example_output.log"
}

# Function to run client sampling example (server-to-client sampling)
run_sampling_example() {
    echo ""
    echo "========================================"
    echo "  Sampling Client Example (Server-to-Client Sampling)"
    echo "========================================"
    echo ""

    build_example "${EXAMPLE_DIR}/client_example/sampling_example" "Sampling Example"
    run_example "${EXAMPLE_DIR}/client_example/sampling_example" "Sampling Example" "SamplingExample" "${EXAMPLE_DIR}/client_example/sampling_example/build/sampling_example_output.log"
}

# Run examples based on type
case "${EXAMPLE_TYPE}" in
    all)
        run_server_example
        run_tool_example
        run_prompt_example
        run_resource_example
        run_sampling_example
        ;;
    server)
        run_server_example
        ;;
    client)
        run_tool_example
        run_prompt_example
        run_resource_example
        run_sampling_example
        ;;
    tool)
        run_tool_example
        ;;
    prompt)
        run_prompt_example
        ;;
    resource)
        run_resource_example
        ;;
    sampling)
        run_sampling_example
        ;;
esac

echo ""
echo "========================================"
echo "  All Examples Completed!"
echo "========================================"
echo ""
