aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_st_TEST_SOURCES)

add_executable(mcp_st_tests
    ${MCP_st_TEST_SOURCES}
)

target_include_directories(mcp_st_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include/mcp
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(mcp_st_tests PRIVATE $<TARGET_OBJECTS:mcp_client>)
target_sources(mcp_st_tests PRIVATE $<TARGET_OBJECTS:mcp_server>)
target_link_libraries(mcp_st_tests
    PRIVATE
        mcp_shared
        mcp_log
        mcp_event
        mcp_net
        mcp_transport
        mcp_auth
        third_party_headers
        http_parser
        nlohmann_json::nlohmann_json
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        event
        pthread
        GTest::gtest_main
)

# Link libevent pthread support
if(TARGET event_pthreads)
    target_link_libraries(mcp_st_tests PRIVATE event_pthreads)
elseif(TARGET libevent_pthreads)
    target_link_libraries(mcp_st_tests PRIVATE libevent_pthreads)
elseif(TARGET "libevent::pthread")
    target_link_libraries(mcp_st_tests PRIVATE libevent::pthread)
else()
    find_library(_mcp_event_pthreads_lib NAMES event_pthreads libevent_pthreads)
    if(_mcp_event_pthreads_lib)
        target_link_libraries(mcp_st_tests PRIVATE ${_mcp_event_pthreads_lib})
    endif()
endif()

target_compile_features(mcp_st_tests PRIVATE cxx_std_17)

gtest_discover_tests(mcp_st_tests)
