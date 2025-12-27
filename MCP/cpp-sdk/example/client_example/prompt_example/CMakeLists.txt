cmake_minimum_required(VERSION 3.10)
project(PromptExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(PromptExample prompt_example.cpp)

target_include_directories(PromptExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../output/mcp
)

target_link_libraries(PromptExample PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../output/libmcp.so
)
