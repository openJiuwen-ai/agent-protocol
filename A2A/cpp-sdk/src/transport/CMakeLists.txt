aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_TRANSPORT_SOURCES)

add_library(a2a_transport OBJECT ${A2A_TRANSPORT_SOURCES})
target_link_libraries(a2a_transport PRIVATE third_party_headers)

target_include_directories(a2a_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/log
        ${CMAKE_SOURCE_DIR}/src/event
        ${CMAKE_SOURCE_DIR}/src/server
)

set_target_properties(a2a_transport PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)