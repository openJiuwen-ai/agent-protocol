cmake_minimum_required(VERSION 3.15)
project(a2a_cpp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(A2A_BUILD_CLIENT "Build A2A client components" ON)
option(A2A_BUILD_SERVER "Build A2A server components" ON)
option(A2A_ENABLE_EXAMPLES "Build example binaries" OFF)
option(A2A_ENABLE_TESTS "Build unit tests" OFF)
option(A2A_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(A2A_ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
    link_libraries(--coverage)
endif()

if(DEFINED ASAN AND ASAN STREQUAL "enable")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

include(${CMAKE_SOURCE_DIR}/third_party/third_party.cmake)

add_subdirectory(src)

if(A2A_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

set(_a2a_link_libs pthread dl OpenSSL::SSL OpenSSL::Crypto http_parser)

if(TARGET event)
    list(APPEND _a2a_link_libs event)
elseif(TARGET libevent)
    list(APPEND _a2a_link_libs libevent)
endif()

if(TARGET event_pthreads)
    list(APPEND _a2a_link_libs event_pthreads)
elseif(TARGET libevent_pthreads)
    list(APPEND _a2a_link_libs libevent_pthreads)
else()
    find_library(_a2a_event_pthreads_lib NAMES event_pthreads libevent_pthreads)
    if(_a2a_event_pthreads_lib)
        list(APPEND _a2a_link_libs ${_a2a_event_pthreads_lib})
    endif()
endif()

if(TARGET nlohmann_json::nlohmann_json)
    list(APPEND _a2a_link_libs nlohmann_json::nlohmann_json)
endif()

if(TARGET CURL::libcurl)
    list(APPEND _a2a_link_libs CURL::libcurl)
elseif(TARGET libcurl)
    list(APPEND _a2a_link_libs libcurl)
endif()

target_link_libraries(a2a PUBLIC ${_a2a_link_libs})

set_target_properties(a2a PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/lib
)

if(A2A_ENABLE_EXAMPLES)
    foreach(_example_target helloworld_client helloworld_server streaming_client streaming_server)
        if(TARGET ${_example_target})
            set_target_properties(${_example_target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/bin
            )
        endif()
    endforeach()
endif()

if(A2A_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
