cmake_minimum_required(VERSION 3.15)

project(mcp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MCP_ENABLE_TESTS "Build unit tests" OFF)
option(MCP_ENABLE_EXAMPLES "Build examples" OFF)
option(MCP_ENABLE_COVERAGE "Enable code coverage" OFF)

include(third_party/third_party.cmake)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_compile_options(-fPIC)
add_compile_options(-ftrapv)

if(MCP_ENABLE_COVERAGE)
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
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -D_FORTIFY_SOURCE=2")
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
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(CMAKE_SKIP_RPATH TRUE)
endif()

# Ensure debug symbols are included
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in Debug mode with full debug symbols")
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "Building in Release with Debug Info mode")
endif()

# Add all sources and target definition for mcp from src/
add_subdirectory(src)

## Link against libevent. Prefer the imported target provided by FetchContent or the
## package config; fall back to `event` which is what libevent's CMake creates when
## fetched by FetchContent in many setups.
set(_mcp_libevent_target "")
if(TARGET event)
	set(_mcp_libevent_target event)
elseif(TARGET libevent)
	set(_mcp_libevent_target libevent)
elseif(TARGET "libevent::event")
	set(_mcp_libevent_target libevent::event)
elseif(TARGET libevent_core)
	set(_mcp_libevent_target libevent_core)
else()
	message(WARNING "No imported libevent target found; attempting to link 'event' (may fail).")
	set(_mcp_libevent_target event)
endif()

target_link_libraries(mcp PUBLIC ${_mcp_libevent_target} nlohmann_json::nlohmann_json CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(mcp PRIVATE http_parser)

# Pull in libevent's pthread helper when available so evthread_use_pthreads resolves.
if(TARGET event_pthreads)
	target_link_libraries(mcp PUBLIC event_pthreads)
elseif(TARGET libevent_pthreads)
	target_link_libraries(mcp PUBLIC libevent_pthreads)
elseif(TARGET "libevent::pthread")
	target_link_libraries(mcp PUBLIC libevent::pthread)
else()
	find_library(_mcp_event_pthreads_lib NAMES event_pthreads libevent_pthreads)
	if(_mcp_event_pthreads_lib)
		target_link_libraries(mcp PUBLIC ${_mcp_event_pthreads_lib})
	else()
		message(WARNING "libevent pthread helper target not found; thread support may fail at link time.")
	endif()
endif()

set_target_properties(mcp PROPERTIES
	OUTPUT_NAME "mcp"
)

if(MCP_ENABLE_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(MCP_ENABLE_EXAMPLES)
	add_subdirectory(examples)
endif()
