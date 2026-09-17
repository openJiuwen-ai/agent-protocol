# Sources and target definition for the mcp library (modularized by subdirectory)

add_subdirectory(auth)
add_subdirectory(log)
add_subdirectory(event)
add_subdirectory(net)

if(MCP_BUILD_SERVER)
    add_subdirectory(server)
endif()

if(MCP_BUILD_CLIENT)
    add_subdirectory(client)
endif()

add_subdirectory(shared)
add_subdirectory(transport)

add_library(mcp SHARED)

target_sources(mcp
    PRIVATE
        $<TARGET_OBJECTS:mcp_auth>
        $<TARGET_OBJECTS:mcp_log>
        $<TARGET_OBJECTS:mcp_event>
        $<TARGET_OBJECTS:mcp_net>
        $<TARGET_OBJECTS:mcp_shared>
        $<TARGET_OBJECTS:mcp_transport>
)

if(MCP_BUILD_SERVER)
    target_sources(mcp PRIVATE $<TARGET_OBJECTS:mcp_server>)
endif()

if(MCP_BUILD_CLIENT)
    target_sources(mcp PRIVATE $<TARGET_OBJECTS:mcp_client>)
endif()

# Public include paths: src/ (for internal headers) and top-level include/
# so that consumers can do #include "mcp_log.h" etc.
target_include_directories(mcp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include/mcp
)

set(MCP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")

add_custom_command(TARGET mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MCP_OUTPUT_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:mcp>" "${MCP_OUTPUT_DIR}/lib/$<TARGET_FILE_NAME:mcp>"
    COMMENT "Copying mcp shared library to ${MCP_OUTPUT_DIR}/lib"
)
