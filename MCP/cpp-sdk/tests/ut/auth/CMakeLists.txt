aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_AUTH_TEST_SOURCES)

add_executable(mcp_auth_tests
    ${MCP_AUTH_TEST_SOURCES}
)

target_include_directories(mcp_auth_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include/mcp
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(mcp_auth_tests PRIVATE $<TARGET_OBJECTS:mcp_auth>)

target_link_libraries(mcp_auth_tests
    PRIVATE
        third_party_headers
        OpenSSL::SSL
        OpenSSL::Crypto
        GTest::gtest_main
)

target_compile_features(mcp_auth_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_auth_tests)
