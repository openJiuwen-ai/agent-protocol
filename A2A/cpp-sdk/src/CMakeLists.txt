# Sources and target definition for the a2a library (modularized by subdirectory)

add_library(a2a SHARED)

add_compile_options(
    -Werror
)

if(A2A_BUILD_CLIENT)
    add_subdirectory(client)
    target_sources(a2a
        PRIVATE
            $<TARGET_OBJECTS:a2a_client>
    )
endif()

if(A2A_BUILD_SERVER)
    add_subdirectory(server)
    add_subdirectory(transport)
    target_sources(a2a
        PRIVATE
            $<TARGET_OBJECTS:a2a_server>
            $<TARGET_OBJECTS:a2a_transport>
    )
endif()

add_subdirectory(event)
target_sources(a2a
    PRIVATE
        $<TARGET_OBJECTS:a2a_event>
)

add_subdirectory(shared)
add_subdirectory(log)
add_subdirectory(utils)

target_sources(a2a
    PRIVATE
        $<TARGET_OBJECTS:a2a_shared>
        $<TARGET_OBJECTS:a2a_log>
        $<TARGET_OBJECTS:a2a_utils>
)

# Public include paths: src/ (for internal headers) and top-level include/
# so that customers can do #include "a2a_log.h" etc.
target_include_directories(a2a
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

set(A2A_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")

find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
)

set(A2A_COMMON_HEADERS
    ${CMAKE_SOURCE_DIR}/include/a2a_log.h
    ${CMAKE_SOURCE_DIR}/include/error.h
    ${CMAKE_SOURCE_DIR}/include/types.h
)

add_custom_command(TARGET a2a POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${A2A_OUTPUT_DIR}/include"
)

foreach(header ${A2A_COMMON_HEADERS})
    add_custom_command(TARGET a2a POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${header}" "${A2A_OUTPUT_DIR}/include/"
    )
endforeach()

if(A2A_BUILD_CLIENT)
    set(A2A_CLIENT_HEADERS
        ${CMAKE_SOURCE_DIR}/include/client/a2a_card_resolver.h
        ${CMAKE_SOURCE_DIR}/include/client/client_call_interceptor.h
        ${CMAKE_SOURCE_DIR}/include/client/client_factory.h
        ${CMAKE_SOURCE_DIR}/include/client/client.h
        ${CMAKE_SOURCE_DIR}/include/client/client_transport.h
        ${CMAKE_SOURCE_DIR}/include/client/jsonrpc_transport.h
    )
    set(A2A_CLIENT_HEADERS
        ${A2A_CLIENT_HEADERS}
        ${CMAKE_SOURCE_DIR}/include/client/http_card_resolver_builder.h
    )

    add_custom_command(TARGET a2a POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${A2A_OUTPUT_DIR}/include/client"
    )

    foreach(header ${A2A_CLIENT_HEADERS})
        add_custom_command(TARGET a2a POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${header}" "${A2A_OUTPUT_DIR}/include/client"
        )
    endforeach()
endif()

if(A2A_BUILD_SERVER)
    set(A2A_SERVER_HEADERS
        ${CMAKE_SOURCE_DIR}/include/server/agent_executor.h
        ${CMAKE_SOURCE_DIR}/include/server/request_context.h
        ${CMAKE_SOURCE_DIR}/include/server/server_call_context.h
        ${CMAKE_SOURCE_DIR}/include/server/server.h
        ${CMAKE_SOURCE_DIR}/include/server/task_store.h
        ${CMAKE_SOURCE_DIR}/include/server/task_updater.h
    )
    set(A2A_SERVER_HEADERS
        ${A2A_SERVER_HEADERS}
        ${CMAKE_SOURCE_DIR}/include/server/http_server_builder.h
    )

    add_custom_command(TARGET a2a POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${A2A_OUTPUT_DIR}/include/server"
    )

    foreach(header ${A2A_SERVER_HEADERS})
        add_custom_command(TARGET a2a POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${header}" "${A2A_OUTPUT_DIR}/include/server"
        )
    endforeach()
endif()