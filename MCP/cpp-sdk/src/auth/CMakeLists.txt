aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_AUTH_SOURCES)

add_library(mcp_auth OBJECT ${MCP_AUTH_SOURCES})
target_link_libraries(mcp_auth PRIVATE third_party_headers)

target_include_directories(mcp_auth
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set_target_properties(mcp_auth PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
