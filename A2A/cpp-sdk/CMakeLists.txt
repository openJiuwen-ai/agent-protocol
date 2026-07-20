cmake_minimum_required(VERSION 3.15)

project(a2a LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(A2A_ENABLE_TESTS "Build unit tests" OFF)
option(A2A_ENABLE_EXAMPLES "Build examples" OFF)
option(A2A_ENABLE_COVERAGE "Enable code coverage" OFF)

include(third_party/third_party.cmake)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_compile_options(-fPIC)
add_compile_options(-ftrapv)

if(A2A_ENABLE_COVERAGE)
    message(STATUS "Code coverage is enabled")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fprofile-arcs -ftest-coverage)
        add_link_options(-fprofile-arcs -ftest-coverage)
    else()
        message(WARNING "Code coverage not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# Set compiler flags based on build type
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")

# Common compiler flags
add_compile_options(-Wall -Werror -Wno-error=unused-variable -Wfloat-equal -Wtrampolines -fno-strict-aliasing -fstack-protector-strong)

# Linker flags for security hardening
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,noexecstack,-z,relro,-z,now -rdynamic -pie")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,noexecstack,-z,relro,-z,now -rdynamic")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
endif()

# Skip RPATH to avoid hardcoded paths in binaries
set(CMAKE_SKIP_RPATH TRUE)

# Ensure debug symbols are included
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in Debug mode with full debug symbols")
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "Building in Release with Debug Info mode")
endif()

# Add all sources and target definition for a2a from src/
add_subdirectory(src)

## Link against libevent. Prefer the imported target provided by FetchContent or the
## package config; fall back to `event` which is what libevent's CMake creates when
## fetched by FetchContent in many setups.
set(_a2a_libevent_target "")
if(TARGET event)
	set(_a2a_libevent_target event)
elseif(TARGET libevent)
	set(_a2a_libevent_target libevent)
elseif(TARGET "libevent::event")
	set(_a2a_libevent_target libevent::event)
elseif(TARGET libevent_core)
	set(_a2a_libevent_target libevent_core)
else()
	message(WARNING "No imported libevent target found; attempting to link 'event' (may fail).")
	set(_a2a_libevent_target event)
endif()

target_link_libraries(a2a PUBLIC ${_a2a_libevent_target} nlohmann_json::nlohmann_json http_parser CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)

# Pull in libevent's pthread helper when available so evthread_use_pthreads resolves.
if(TARGET event_pthreads)
    target_link_libraries(a2a PUBLIC event_pthreads)
elseif(TARGET libevent_pthreads)
    target_link_libraries(a2a PUBLIC libevent_pthreads)
elseif(TARGET "libevent::pthread")
    target_link_libraries(a2a PUBLIC libevent::pthread)
else()
    find_library(_a2a_event_pthreads_lib NAMES event_pthreads libevent_pthreads)
    if(_a2a_event_pthreads_lib)
        target_link_libraries(a2a PUBLIC ${_a2a_event_pthreads_lib})
    else()
        message(WARNING "libevent pthread helper target not found; thread support may fail at link time.")
    endif()
endif()

set_target_properties(a2a PROPERTIES
    OUTPUT_NAME "a2a"
)

if(TARGET http_parser)
    target_link_libraries(a2a PRIVATE http_parser)
endif()

if (TARGET OpenSSL::SSL AND OpenSSL::Crypto)
    target_link_libraries(a2a PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

if(TARGET CURL::libcurl)
    target_link_libraries(a2a PUBLIC CURL::libcurl)
endif()

if(A2A_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(A2A_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()