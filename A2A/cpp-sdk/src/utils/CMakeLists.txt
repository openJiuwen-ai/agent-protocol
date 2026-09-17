aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_UTILS_SOURCES)

add_library(a2a_utils OBJECT ${A2A_UTILS_SOURCES})
target_link_libraries(a2a_utils PRIVATE third_party_headers)

target_include_directories(a2a_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/shared
)

set_target_properties(a2a_utils PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)