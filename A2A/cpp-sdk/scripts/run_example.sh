
#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

set -e

SCRIPT_PATH=$(cd $(dirname $0);pwd)
LIB_PATH=${SCRIPT_PATH}/../output/lib
BIN_PATH=${SCRIPT_PATH}/../output/bin

export LD_LIBRARY_PATH="$LIB_PATH:$LD_LIBRARY_PATH"

function check_example_process()
{
    local name="$1"
    local pid=$(ps -ef | grep $name |  grep -v grep | awk -F ' ' '{print $2}')
    if [ "${pid}X" == "X" ]; then
        echo -e "\033[31mserver exit unexpectedly\033][0m"
        exit 1
    fi
}

function clear_example_process()
{
    local name="$1"
    local pid=$(ps -ef | grep $name | grep -v grep | awk -F ' ' '{print $2}')
    if [ "${pid}X" == "X" ]; then
        return 0
    fi

    kill ${pid}
    sleep 1
}

function run_helloworld_example()
{
    $BIN_PATH/helloworld_server -i 127.0.0.1 -p 8888 &
    sleep 1
    $BIN_PATH/helloworld_client -i 127.0.0.1 -p 8888

    check_example_process "helloworld_server"
    clear_example_process "helloworld_server"
}

function run_streaming_example()
{
    $BIN_PATH/streaming_server -i 127.0.0.1 -p 8888 &
    sleep 1
    $BIN_PATH/streaming_client -i 127.0.0.1 -p 8888

    check_example_process "streaming_server"
    clear_example_process "streaming_server"
}

run_helloworld_example
run_streaming_example
