cmake_minimum_required(VERSION 3.10)
project(ServerExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ServerExample server_example.cpp)

target_include_directories(ServerExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include/mcp
)

target_link_libraries(ServerExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../output/lib/libmcp.so
)
