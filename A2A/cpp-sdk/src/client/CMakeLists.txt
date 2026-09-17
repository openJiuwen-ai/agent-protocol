aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_CLIENT_SOURCES)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/connection A2A_CLIENT_CONNECTION_SOURCES)

add_library(a2a_client OBJECT ${A2A_CLIENT_SOURCES} ${A2A_CLIENT_CONNECTION_SOURCES})
target_link_libraries(a2a_client PRIVATE third_party_headers)

target_include_directories(a2a_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/event
        ${CMAKE_CURRENT_SOURCE_DIR}/connection
        ${CMAKE_SOURCE_DIR}/src/client
)

set_target_properties(a2a_client PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
