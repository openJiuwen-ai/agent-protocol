cmake_minimum_required(VERSION 3.10)
project(ServerExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MCP_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
include("${MCP_SDK_ROOT}/example/cmake/ExampleCommon.cmake")

add_executable(ServerExample
    server_example.cpp
    simple_token_verifier.cpp
)

target_include_directories(ServerExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

find_package(Threads REQUIRED)

target_link_libraries(ServerExample PRIVATE Threads::Threads)
mcp_link_example_dependencies(ServerExample)
