#!/bin/bash

# Build and run HttpClientService test suite

set -e

echo "HttpClientService Comprehensive Test Suite Build and Run"
echo "======================================================="

# Compile HttpClientService test program with all necessary source files
echo "Building HttpClientService test program..."

g++ -std=c++17 -Wall -Wextra -O2 \
    -I../../src \
    -I../../include/mcp \
    -I../../third_party/libevent-src/include \
    -I../../src/event \
    -I../../src/shared \
    -I../../src/shared/message_queue \
    -I../../src/client/transport \
    -DMCP_LOG_ENABLED=1 \
    http_client_service_test.cpp \
    ../../src/log/mcp_log.cpp \
    ../../src/event/event_system.cpp \
    ../../src/client/transport/http_client_service.cpp \
    ../../src/shared/http_common.cpp \
    -lcurl \
    -levent \
    -levent_pthreads \
    -lpthread \
    -o http_client_service_test

if [ $? -eq 0 ]; then
    echo "SUCCESS: HttpClientService test build completed!"
    echo ""
    echo "Running HttpClientService test suite:"
    echo "  ./http_client_service_test"
    echo ""

    # Run HttpClientService test directly
    echo "Starting HttpClientService test suite..."
    ./http_client_service_test
else
    echo "ERROR: HttpClientService test build failed"
    exit 1
fi
