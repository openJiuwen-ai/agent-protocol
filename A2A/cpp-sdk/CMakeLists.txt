cmake_minimum_required(VERSION 3.15)
project(a2a_cpp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On macOS, Homebrew installs some dependencies (OpenSSL, curl, ...) as keg-only,
# meaning they are not on the default search path. Prepend their prefixes so that
# find_package/find_library can locate them without manual OPENSSL_ROOT_DIR.
if(APPLE)
    find_program(A2A_BREW_EXECUTABLE brew)
    if(A2A_BREW_EXECUTABLE)
        foreach(_a2a_brew_pkg openssl@3 openssl curl libevent nlohmann-json)
            execute_process(
                COMMAND ${A2A_BREW_EXECUTABLE} --prefix ${_a2a_brew_pkg}
                OUTPUT_VARIABLE _a2a_brew_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            if(_a2a_brew_prefix AND EXISTS "${_a2a_brew_prefix}")
                list(APPEND CMAKE_PREFIX_PATH "${_a2a_brew_prefix}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
    endif()
endif()

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
find_package(Threads REQUIRED)

# pthread is provided by the system (Threads::Threads); dl exists only on Linux
# and is exposed via CMAKE_DL_LIBS (empty on macOS).
set(_a2a_link_libs Threads::Threads ${CMAKE_DL_LIBS} OpenSSL::SSL OpenSSL::Crypto http_parser)

if(TARGET event)
    list(APPEND _a2a_link_libs event)
elseif(TARGET libevent)
    list(APPEND _a2a_link_libs libevent)
else()
    # Preferred: imported targets from find_package (Linux distros). Fallback:
    # locate the library by name (macOS/Homebrew ships libevent without a
    # CMake config; the brew prefixes were already added to CMAKE_PREFIX_PATH).
    find_library(_a2a_event_lib NAMES event libevent)
    if(NOT _a2a_event_lib)
        message(FATAL_ERROR
            "libevent not found. Install it first: 'brew install libevent' (macOS) "
            "or 'apt install libevent-dev' (Linux).")
    endif()
    list(APPEND _a2a_link_libs ${_a2a_event_lib})
    # Keep headers and library from the same prefix so the fallback can never
    # pair a system libevent library with third-party headers. The include dir
    # is attached to the shared interface target because event_system.cpp is
    # compiled by object libraries (a2a_event/a2a_shared), not by `a2a` itself.
    get_filename_component(_a2a_event_lib_dir "${_a2a_event_lib}" DIRECTORY)
    if(EXISTS "${_a2a_event_lib_dir}/../include")
        target_include_directories(third_party_headers INTERFACE "${_a2a_event_lib_dir}/../include")
    endif()
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
