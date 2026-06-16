aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_EVENT_SOURCES)

add_library(a2a_event OBJECT ${A2A_EVENT_SOURCES})
target_link_libraries(a2a_event PRIVATE third_party_headers)

target_include_directories(a2a_event
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(a2a_event PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)