#!/usr/bin/env bash
# Smoke-test example servers for yellow_a2a.
# Binaries and liba2a.so are under output/bin and output/lib (see root CMakeLists.txt).
#
# Prerequisite:
#   bash ./scripts/build.sh -e
#
# Usage:
#   bash ./scripts/run_example.sh
#   A2A_EXAMPLE_PORT=9999 bash ./scripts/run_example.sh

set -euo pipefail

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_PATH}/.." && pwd)"
OUTPUT_DIR="${A2A_OUTPUT_DIR:-${ROOT}/output}"
EXAMPLE_BIN="${OUTPUT_DIR}/bin"
LIB_DIR="${OUTPUT_DIR}/lib"

export LD_LIBRARY_PATH="${LIB_DIR}:${LD_LIBRARY_PATH:-}"

PORT="${A2A_EXAMPLE_PORT:-8888}"
JSONRPC_BASE="http://127.0.0.1:${PORT}/jsonrpc"
CARD_URL="http://127.0.0.1:${PORT}/.well-known/agent-card.json"

require_bins() {
    local missing=0
    for bin in helloworld_server helloworld_client streaming_server streaming_client; do
        if [[ ! -x "${EXAMPLE_BIN}/${bin}" ]]; then
            echo "Missing: ${EXAMPLE_BIN}/${bin}" >&2
            missing=1
        fi
    done
    if [[ ${missing} -ne 0 ]]; then
        echo "Build examples first: (cd \"${ROOT}\" && bash scripts/build.sh -e)" >&2
        exit 1
    fi
}

command -v curl >/dev/null 2>&1 || {
    echo "curl is required for HTTP/JSON-RPC checks." >&2
    exit 1
}

check_example_process() {
    local name="$1"
    local pid
    pid=$(pgrep -f "${name}" 2>/dev/null | head -1 || true)
    if [[ -z "${pid}" ]]; then
        echo -e "\033[31m${name} exited unexpectedly\033[0m" >&2
        exit 1
    fi
}

clear_example_process() {
    local name="$1"
    local pids
    pids=$(pgrep -f "${name}" 2>/dev/null || true)
    if [[ -z "${pids}" ]]; then
        return 0
    fi
    # Kill server processes started by this script (match binary name in argv)
    while read -r pid; do
        [[ -n "${pid}" ]] && kill "${pid}" 2>/dev/null || true
    done <<< "${pids}"
    sleep 1
}

jsonrpc_expect_result() {
    local body="$1"
    local label="$2"
    local resp
    if ! resp=$(curl -sS -X POST "${JSONRPC_BASE}" -H 'Content-Type: application/json' -d "${body}"); then
        echo "${label}: curl failed" >&2
        exit 1
    fi
    if ! echo "${resp}" | grep -q '"result"'; then
        echo "${label}: expected JSON-RPC result, got:" >&2
        echo "${resp}" >&2
        exit 1
    fi
}

expect_agent_card_http() {
    local label="$1"
    local resp
    if ! resp=$(curl -sS "${CARD_URL}"); then
        echo "${label}: GET agent card failed" >&2
        exit 1
    fi
    if ! echo "${resp}" | grep -q '"name"'; then
        echo "${label}: expected agent card JSON with name, got:" >&2
        echo "${resp}" >&2
        exit 1
    fi
}

run_helloworld_example() {
    clear_example_process "helloworld_server"
    "${EXAMPLE_BIN}/helloworld_server" -i 127.0.0.1 -p "${PORT}" &
    sleep 1

    expect_agent_card_http "helloworld agent card (HTTP)"
    jsonrpc_expect_result \
        '{"jsonrpc":"2.0","method":"GetAgentCard","id":"1"}' \
        "helloworld GetAgentCard"
    local hello_send_msg='{"jsonrpc":"2.0","method":"SendMessage","id":"2","params":'
    hello_send_msg+='{"message":{"messageId":"m1","role":"ROLE_USER",'
    hello_send_msg+='"parts":[{"text":"hello","mediaType":"text/plain"}]}}}'
    jsonrpc_expect_result "${hello_send_msg}" "helloworld SendMessage"

    echo "Running helloworld_client against 127.0.0.1:${PORT}..."
    timeout 30 "${EXAMPLE_BIN}/helloworld_client" -i 127.0.0.1 -p "${PORT}"

    check_example_process "helloworld_server"
    clear_example_process "helloworld_server"
}

run_streaming_example() {
    clear_example_process "streaming_server"
    "${EXAMPLE_BIN}/streaming_server" -i 127.0.0.1 -p "${PORT}" &
    sleep 1

    expect_agent_card_http "streaming agent card (HTTP)"
    jsonrpc_expect_result \
        '{"jsonrpc":"2.0","method":"GetAgentCard","id":"1"}' \
        "streaming GetAgentCard"
    local streaming_send_msg='{"jsonrpc":"2.0","method":"SendMessage","id":"2","params":'
    streaming_send_msg+='{"message":{"messageId":"m2","role":"ROLE_USER",'
    streaming_send_msg+='"parts":[{"data":{"destination":"Paris","date":"tomorrow"}}]}}}'
    jsonrpc_expect_result "${streaming_send_msg}" "streaming SendMessage"

    echo "Running streaming_client against 127.0.0.1:${PORT}..."
    timeout 60 "${EXAMPLE_BIN}/streaming_client" -i 127.0.0.1 -p "${PORT}"

    check_example_process "streaming_server"
    clear_example_process "streaming_server"
}

require_bins
run_helloworld_example
run_streaming_example
echo "Example smoke tests passed."
