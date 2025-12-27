cmake_minimum_required(VERSION 3.10)
project(ResourceExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ResourceExample resource_example.cpp)

target_include_directories(ResourceExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../output/mcp
)

target_link_libraries(ResourceExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../output/libmcp.so
)
