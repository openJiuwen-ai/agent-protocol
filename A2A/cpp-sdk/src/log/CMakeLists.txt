aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_LOG_SOURCES)

add_library(a2a_log OBJECT ${A2A_LOG_SOURCES})
target_link_libraries(a2a_log PRIVATE third_party_headers)

target_include_directories(a2a_log
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(a2a_log PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
