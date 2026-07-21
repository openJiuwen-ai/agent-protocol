cmake_minimum_required(VERSION 3.10)
project(ResourceExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MCP_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${MCP_SDK_ROOT}/example/cmake/ExampleCommon.cmake")

add_executable(ResourceExample resource_example.cpp)

mcp_link_example_dependencies(ResourceExample)
