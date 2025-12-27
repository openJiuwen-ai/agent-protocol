aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_LOG_SOURCES)

add_library(mcp_log OBJECT ${MCP_LOG_SOURCES})
target_link_libraries(mcp_log PRIVATE third_party_headers)

target_include_directories(mcp_log
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set_target_properties(mcp_log PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
