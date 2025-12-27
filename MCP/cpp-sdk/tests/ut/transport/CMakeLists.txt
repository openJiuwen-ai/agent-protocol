aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_transport_TEST_SOURCES)

add_executable(mcp_transport_tests
    ${MCP_transport_TEST_SOURCES}
)

target_include_directories(mcp_transport_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include/mcp
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(mcp_transport_tests PRIVATE $<TARGET_OBJECTS:mcp_transport>)

target_link_libraries(mcp_transport_tests
    PRIVATE
        mcp_shared
        mcp_log
        mcp_event
        mcp_net
        third_party_headers
        http_parser
        event
        pthread
        GTest::gtest_main
)

# Link libevent pthread support
if(TARGET event_pthreads)
    target_link_libraries(mcp_transport_tests PRIVATE event_pthreads)
elseif(TARGET libevent_pthreads)
    target_link_libraries(mcp_transport_tests PRIVATE libevent_pthreads)
elseif(TARGET "libevent::pthread")
    target_link_libraries(mcp_transport_tests PRIVATE libevent::pthread)
endif()

target_compile_features(mcp_transport_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_transport_tests)
