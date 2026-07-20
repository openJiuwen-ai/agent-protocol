#!/usr/bin/env bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Smoke-test example servers built by scripts/build.sh -e (default tree: ../build/).
# CMake places targets under build/examples/ and liba2a.so under build/src/.

set -euo pipefail

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_PATH}/.." && pwd)"
# Match scripts/build.sh default (-b build). Override with A2A_BUILD_DIR if you use -b elsewhere.
BUILD_DIR_NAME="${A2A_BUILD_DIR:-build}"
BUILD="${ROOT}/${BUILD_DIR_NAME}"
EXAMPLE_BIN="${BUILD}/examples"
# liba2a.so lives under src/; keep build root for any other shared libs
export LD_LIBRARY_PATH="${BUILD}/src:${BUILD}:${LD_LIBRARY_PATH:-}"

PORT="${A2A_EXAMPLE_PORT:-8888}"

require_bins() {
    if [[ ! -x "${EXAMPLE_BIN}/helloworld_server" ]] || [[ ! -x "${EXAMPLE_BIN}/streaming_server" ]] \
        || [[ ! -x "${EXAMPLE_BIN}/helloworld_client" ]] || [[ ! -x "${EXAMPLE_BIN}/streaming_client" ]]; then
        echo "Missing example binaries under ${EXAMPLE_BIN}." >&2
        echo "Build with: (cd \"${ROOT}\" && bash scripts/build.sh -e)" >&2
        exit 1
    fi
}

command -v curl >/dev/null 2>&1 || {
    echo "curl is required for JSON-RPC checks." >&2
    exit 1
}

check_example_process() {
    local name="$1"
    local pid
    pid=$(ps -ef | grep "${name}" | grep -v grep | awk '{print $2}' | head -1)
    if [[ -z "${pid}" ]]; then
        echo -e "\033[31mserver exit unexpectedly\033[0m" >&2
        exit 1
    fi
}

clear_example_process() {
    local name="$1"
    local pid
    pid=$(ps -ef | grep "${name}" | grep -v grep | awk '{print $2}' | head -1)
    if [[ -z "${pid}" ]]; then
        return 0
    fi
    kill "${pid}" 2>/dev/null || true
    sleep 1
}

jsonrpc_expect_result() {
    local url="$1"
    local body="$2"
    local label="$3"
    local resp
    if ! resp=$(curl -sS -X POST "${url}" -H 'Content-Type: application/json' -d "${body}"); then
        echo "${label}: curl failed" >&2
        exit 1
    fi
    if ! echo "${resp}" | grep -q '"result"'; then
        echo "${label}: expected JSON-RPC result, got:" >&2
        echo "${resp}" >&2
        exit 1
    fi
}

run_helloworld_example() {
    local base="http://127.0.0.1:${PORT}/jsonrpc"
    "${EXAMPLE_BIN}/helloworld_server" -i 127.0.0.1 -p "${PORT}" &
    sleep 1

    jsonrpc_expect_result "${base}" '{"jsonrpc":"2.0","method":"agent.card","id":1}' "helloworld agent.card"
    jsonrpc_expect_result "${base}" \
        '{"jsonrpc":"2.0","method":"message/send","id":2,"params":{"message":{"messageId":"m1","role":"user","parts":[{"kind":"text","text":"hello"}]}}}' \
        "helloworld message/send"

    echo "Running helloworld_client against 127.0.0.1:${PORT}..."
    (sleep 1 && printf '\n') | "${EXAMPLE_BIN}/helloworld_client" -i 127.0.0.1 -p "${PORT}"

    check_example_process "helloworld_server"
    clear_example_process "helloworld_server"
}

run_streaming_example() {
    local base="http://127.0.0.1:${PORT}/jsonrpc"
    "${EXAMPLE_BIN}/streaming_server" -i 127.0.0.1 -p "${PORT}" &
    sleep 1

    jsonrpc_expect_result "${base}" '{"jsonrpc":"2.0","method":"agent.card","id":1}' "streaming agent.card"
    jsonrpc_expect_result "${base}" \
        '{"jsonrpc":"2.0","method":"message/send","id":2,"params":{"message":{"messageId":"m2","role":"user","parts":[{"kind":"data","data":{"destination":"Paris","date":"tomorrow"}}]}}}' \
        "streaming message/send"

    echo "Running streaming_client against 127.0.0.1:${PORT}..."
    (sleep 1 && printf '\n') | "${EXAMPLE_BIN}/streaming_client" -i 127.0.0.1 -p "${PORT}"

    check_example_process "streaming_server"
    clear_example_process "streaming_server"
}

require_bins
run_helloworld_example
run_streaming_example
echo "Example smoke tests passed."
