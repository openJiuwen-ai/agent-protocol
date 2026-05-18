aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_server_TEST_SOURCES)

add_executable(mcp_server_tests
    ${MCP_server_TEST_SOURCES}
)

target_include_directories(mcp_server_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/shared
)

target_sources(mcp_server_tests PRIVATE $<TARGET_OBJECTS:mcp_server>)

target_link_libraries(mcp_server_tests
     PRIVATE
         mcp_shared
         mcp_log
         third_party_headers
         mcp_event
         mcp_net
         mcp_transport
         http_parser
         OpenSSL::SSL
         OpenSSL::Crypto
         event
         pthread
         gtest
         gtest_main
         ${_mcp_event_pthreads_target}
)


target_compile_features(mcp_server_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_server_tests)
