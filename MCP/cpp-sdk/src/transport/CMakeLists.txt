# Transport module is excluded from compilation
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_TRANSPORT_SOURCES)

if(NOT MCP_WITH_HTTP)
    list(FILTER MCP_TRANSPORT_SOURCES EXCLUDE REGEX ".*http.*")
endif()

add_library(mcp_transport OBJECT ${MCP_TRANSPORT_SOURCES})
target_link_libraries(mcp_transport PRIVATE third_party_headers)

# Set include directories for this module
target_include_directories(mcp_transport
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include/mcp
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/shared
)

# Link libevent for event system
target_link_libraries(mcp_transport PRIVATE event)
