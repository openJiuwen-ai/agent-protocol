aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_SERVER_SOURCES)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/events A2A_SERVER_EVENTS_SOURCES)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/net A2A_SERVER_NET_SOURCES)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/tasks A2A_SERVER_TASKS_SOURCES)

add_library(a2a_server OBJECT ${A2A_SERVER_SOURCES} ${A2A_SERVER_EVENTS_SOURCES} ${A2A_SERVER_NET_SOURCES} ${A2A_SERVER_TASKS_SOURCES})
target_link_libraries(a2a_server PRIVATE third_party_headers)

target_include_directories(a2a_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/event
        ${CMAKE_SOURCE_DIR}/src/log
        ${CMAKE_SOURCE_DIR}/src/server
        ${CMAKE_SOURCE_DIR}/src/transport
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_LIST_DIR}/tasks
        ${CMAKE_CURRENT_LIST_DIR}/net
        ${CMAKE_CURRENT_LIST_DIR}/events
)

set_target_properties(a2a_server PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)