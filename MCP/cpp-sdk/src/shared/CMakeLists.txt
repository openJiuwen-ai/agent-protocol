aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_SHARED_SOURCES)

add_library(mcp_shared OBJECT ${MCP_SHARED_SOURCES})
target_link_libraries(mcp_shared PRIVATE third_party_headers)

target_include_directories(mcp_shared
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set_target_properties(mcp_shared PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
