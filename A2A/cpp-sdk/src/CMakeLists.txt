
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} A2A_SOURCES)
add_library(a2a_cpp SHARED ${A2A_SOURCES})

set_target_properties(a2a_cpp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME ${PROJECT_NAME}
    POSITION_INDEPENDENT_CODE ON
    LINKER_LANGUAGE CXX
)

if(OHOS)
    target_link_options(a2a_cpp PRIVATE -Wl,-soname,liba2a_cpp.so)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src/client)
include_directories(${CMAKE_SOURCE_DIR}/src/server)
include_directories(${CMAKE_SOURCE_DIR}/src/server/tasks)
include_directories(${CMAKE_SOURCE_DIR}/src/server/events)
include_directories(${CMAKE_SOURCE_DIR}/src/transport)

add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(transport)
add_subdirectory(utils)

target_link_libraries(a2a_cpp PUBLIC nlohmann_json::nlohmann_json dl pthread)

if (A2A_HTTP AND CURL_FOUND)
    target_link_libraries(a2a_cpp PUBLIC CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)
endif()
