aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_SHARED_SOURCES)

add_library(a2a_shared OBJECT ${A2A_SHARED_SOURCES})
target_link_libraries(a2a_shared PRIVATE third_party_headers)

target_include_directories(a2a_shared
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(a2a_shared PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
