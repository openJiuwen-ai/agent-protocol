# Sources and target definition for the mcp library (modularized by subdirectory)

add_subdirectory(auth)
add_subdirectory(log)
add_subdirectory(event)
add_subdirectory(net)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(shared)
add_subdirectory(transport)

add_library(mcp SHARED)

target_sources(mcp
    PRIVATE
        $<TARGET_OBJECTS:mcp_auth>
        $<TARGET_OBJECTS:mcp_log>
        $<TARGET_OBJECTS:mcp_event>
        $<TARGET_OBJECTS:mcp_net>
        $<TARGET_OBJECTS:mcp_server>
        $<TARGET_OBJECTS:mcp_client>
        $<TARGET_OBJECTS:mcp_shared>
        $<TARGET_OBJECTS:mcp_transport>
)

# Public include paths: src/ (for internal headers) and top-level include/
# so that consumers can do #include "mcp_log.h" etc.
target_include_directories(mcp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set(MCP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")

add_custom_command(TARGET mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MCP_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:mcp>" "${MCP_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/include" "${MCP_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MCP_OUTPUT_DIR}/mcp/nlohmann"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json-src/single_include/nlohmann" "${MCP_OUTPUT_DIR}/mcp/nlohmann"
    COMMENT "Copying mcp shared library, headers and nlohmann json to ${MCP_OUTPUT_DIR}"
)
