aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_LOG_TEST_SOURCES)

add_executable(mcp_log_tests
    ${MCP_LOG_TEST_SOURCES}
)

target_include_directories(mcp_log_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include/mcp
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(mcp_log_tests PRIVATE $<TARGET_OBJECTS:mcp_log>)

target_link_libraries(mcp_log_tests
    PRIVATE
        third_party_headers
        GTest::gtest_main
)

target_compile_features(mcp_log_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_log_tests)
