aux_source_directory(${CMAKE_CURRENT_LIST_DIR} MCP_SERVER_SOURCES)

if(MCP_WITH_HTTP)
    aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/transport MCP_SERVER_TRANSPORT_SOURCES)
else()
    list(FILTER MCP_SERVER_SOURCES EXCLUDE REGEX ".*http.*")
    set(MCP_SERVER_TRANSPORT_SOURCES "")
endif()

add_library(mcp_server OBJECT ${MCP_SERVER_SOURCES} ${MCP_SERVER_TRANSPORT_SOURCES})
target_link_libraries(mcp_server
    PRIVATE
        http_parser_headers
        third_party_headers
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_include_directories(mcp_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/transport
        ${CMAKE_SOURCE_DIR}/include/mcp
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/net
        ${CMAKE_SOURCE_DIR}/src/event
)

set_target_properties(mcp_server PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
