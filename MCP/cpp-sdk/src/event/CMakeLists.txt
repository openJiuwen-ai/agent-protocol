aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_EVENT_SOURCES)

add_library(mcp_event OBJECT ${MCP_EVENT_SOURCES})
target_link_libraries(mcp_event PRIVATE third_party_headers)

target_include_directories(mcp_event
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set_target_properties(mcp_event PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
