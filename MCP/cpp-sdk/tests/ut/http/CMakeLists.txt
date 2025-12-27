add_executable(mcp_http_client_test
    http_client_test.cpp
)

foreach(target mcp_http_client_test)
    target_link_libraries(${target}
        PRIVATE
            mcp
    )
    target_compile_features(${target} PRIVATE cxx_std_17)
endforeach()
