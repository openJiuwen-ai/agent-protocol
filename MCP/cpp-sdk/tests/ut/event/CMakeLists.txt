aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_EVENT_TEST_SOURCES)

add_executable(mcp_event_system_tests
    ${MCP_EVENT_TEST_SOURCES}
)

target_include_directories(mcp_event_system_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(mcp_event_system_tests PRIVATE $<TARGET_OBJECTS:mcp_event>)

target_link_libraries(mcp_event_system_tests
    PRIVATE
        mcp_log
        third_party_headers
        event
        pthread
        GTest::gtest_main
)

# Link libevent pthread support
if(TARGET event_pthreads)
    target_link_libraries(mcp_event_system_tests PRIVATE event_pthreads)
elseif(TARGET libevent_pthreads)
    target_link_libraries(mcp_event_system_tests PRIVATE libevent_pthreads)
elseif(TARGET "libevent::pthread")
    target_link_libraries(mcp_event_system_tests PRIVATE libevent::pthread)
else()
    find_library(_mcp_event_pthreads_lib NAMES event_pthreads libevent_pthreads)
    if(_mcp_event_pthreads_lib)
        target_link_libraries(mcp_event_system_tests PRIVATE ${_mcp_event_pthreads_lib})
    endif()
endif()

target_compile_features(mcp_event_system_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_event_system_tests)
