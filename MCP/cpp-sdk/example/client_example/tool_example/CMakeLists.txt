cmake_minimum_required(VERSION 3.10)
project(ToolExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ToolExample tool_example.cpp)

target_include_directories(ToolExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../output/mcp
)

target_link_libraries(ToolExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../output/libmcp.so
)
