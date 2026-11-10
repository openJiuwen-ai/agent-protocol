add_executable(mcp_http_client_test
    http_client_test.cpp
)

if(MCP_BUILD_CLIENT)
    foreach(target mcp_http_client_test)
        target_link_libraries(${target}
            PRIVATE
                mcp
        )
        target_compile_features(${target} PRIVATE cxx_std_17)
    endforeach()
endif()

# Compile the real synchronous client against a scripted socket/event loop.
# Keep fake headers private to this target; do not link real network objects.
get_filename_component(_mcp_http_sdk_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_executable(mcp_http_client_callbacks_test
    http_client_callbacks_test.cpp
    ${_mcp_http_sdk_root}/src/client/http_client.cpp
    ${_mcp_http_sdk_root}/src/shared/http_common.cpp
)
target_include_directories(mcp_http_client_callbacks_test BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${_mcp_http_sdk_root}/src
)
target_link_libraries(mcp_http_client_callbacks_test PRIVATE
    GTest::gtest_main
)
target_compile_features(mcp_http_client_callbacks_test PRIVATE cxx_std_17)
gtest_discover_tests(mcp_http_client_callbacks_test DISCOVERY_TIMEOUT 60)
