
include_directories(${CMAKE_SOURCE_DIR}/src/client)
include_directories(${CMAKE_SOURCE_DIR}/src/server)
include_directories(${CMAKE_SOURCE_DIR}/src/server/tasks)
include_directories(${CMAKE_SOURCE_DIR}/src/server/events)
include_directories(${CMAKE_SOURCE_DIR}/src/transport)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_SOURCES)

target_sources(a2a_cpp PRIVATE ${A2A_SOURCES})
