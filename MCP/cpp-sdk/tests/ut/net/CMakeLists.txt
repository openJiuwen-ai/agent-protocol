aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_NET_TEST_SOURCES)

add_executable(mcp_net_tests
    ${MCP_NET_TEST_SOURCES}
)

target_include_directories(mcp_net_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(mcp_net_tests PRIVATE $<TARGET_OBJECTS:mcp_net>)

target_link_libraries(mcp_net_tests
     PRIVATE
         mcp_log
         mcp_event
         third_party_headers
         event
         pthread
         GTest::gtest_main
         ${_mcp_event_pthreads_target}
)

target_compile_features(mcp_net_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_net_tests)
