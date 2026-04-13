cmake_minimum_required(VERSION 3.10)
project(ToolExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ToolExample tool_example.cpp)

set(MCP_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../output")
set(MCP_LIBRARY "${MCP_OUTPUT_DIR}/libmcp${CMAKE_SHARED_LIBRARY_SUFFIX}")

target_include_directories(ToolExample PRIVATE
    ${MCP_OUTPUT_DIR}/mcp
)

target_link_libraries(ToolExample PRIVATE
    ${MCP_LIBRARY}
)

set_target_properties(ToolExample PROPERTIES
    BUILD_RPATH "${MCP_OUTPUT_DIR}"
)
