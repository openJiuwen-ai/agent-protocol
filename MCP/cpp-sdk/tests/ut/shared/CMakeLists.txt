aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_SHARED_TEST_SOURCES)

add_executable(mcp_shared_tests
    ${MCP_SHARED_TEST_SOURCES}
)

target_include_directories(mcp_shared_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mcp_shared_tests
    mcp_shared
    mcp_log
    third_party_headers
    gtest
    gtest_main
)

target_compile_features(mcp_shared_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_shared_tests)