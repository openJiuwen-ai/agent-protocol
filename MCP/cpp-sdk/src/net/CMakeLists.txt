aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_NET_SOURCES)

add_library(mcp_net OBJECT ${MCP_NET_SOURCES})
target_link_libraries(mcp_net PRIVATE third_party_headers)

target_include_directories(mcp_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set_target_properties(mcp_net PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
