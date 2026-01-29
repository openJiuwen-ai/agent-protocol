# Sources and target definition for the a2a library (modularized by subdirectory)

add_subdirectory(client)
add_subdirectory(event)
add_subdirectory(shared)
add_subdirectory(log)
add_subdirectory(server)
add_subdirectory(utils)
add_subdirectory(transport)

add_library(a2a SHARED)

target_sources(a2a
    PRIVATE
        $<TARGET_OBJECTS:a2a_event>
        $<TARGET_OBJECTS:a2a_client>
        $<TARGET_OBJECTS:a2a_shared>
        $<TARGET_OBJECTS:a2a_log>
        $<TARGET_OBJECTS:a2a_server>
        $<TARGET_OBJECTS:a2a_utils>
        $<TARGET_OBJECTS:a2a_transport>
)

# Public include paths: src/ (for internal headers) and top-level include/
# so that customers can do #include "a2a_log.h" etc.
target_include_directories(a2a
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include/a2a
)

set(A2A_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")

find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
)

if(NLOHMANN_JSON_INCLUDE_DIR)
    set(A2A_NLOHMANN_COPY_DIR "${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann")
else()
    set(A2A_NLOHMANN_COPY_DIR "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json-src/single_include/nlohmann")
endif()

add_custom_command(TARGET a2a POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${A2A_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:a2a>" "${A2A_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/include" "${A2A_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${A2A_OUTPUT_DIR}/nlohmann"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${A2A_NLOHMANN_COPY_DIR}" "${A2A_OUTPUT_DIR}/nlohmann"
    COMMENT "Copying a2a shared library, headers and nlohmann json to ${A2A_OUTPUT_DIR}"
)
