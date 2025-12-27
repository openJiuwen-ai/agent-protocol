aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_CLIENT_SOURCES)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/transport MCP_CLIENT_TRANSPORT_SOURCES)

add_library(mcp_client OBJECT ${MCP_CLIENT_SOURCES} ${MCP_CLIENT_TRANSPORT_SOURCES})
target_link_libraries(mcp_client PRIVATE third_party_headers)

target_include_directories(mcp_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include/mcp
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/shared/message_queue
        ${CMAKE_SOURCE_DIR}/src/net
        ${CMAKE_SOURCE_DIR}/src/event
        ${CMAKE_CURRENT_SOURCE_DIR}/transport
        ${CMAKE_SOURCE_DIR}/src/client
        ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(mcp_client PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
