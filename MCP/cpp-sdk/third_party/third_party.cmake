# Centralized third-party dependencies (FetchContent) for mcp_cpp
# This file will try to find system-provided packages first, then fall back to FetchContent.

include(FetchContent)

# Standalone examples set MCP_SDK_ROOT to the cpp-sdk root before including this file.
if(NOT DEFINED MCP_SDK_ROOT)
  set(MCP_SDK_ROOT "${CMAKE_SOURCE_DIR}")
endif()
get_filename_component(MCP_SDK_ROOT "${MCP_SDK_ROOT}" ABSOLUTE)

set(FETCHCONTENT_BASE_DIR "${MCP_SDK_ROOT}/third_party" CACHE PATH "" FORCE)

if(NOT TARGET third_party_headers)
  add_library(third_party_headers INTERFACE)
endif()

function(_mcp_find_libevent_pthreads_library out_var)
  find_library(_mcp_libevent_pthreads_lib
    NAMES event_pthreads event_pthreads-2.1 libevent_pthreads
    PATHS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib
  )
  if(NOT _mcp_libevent_pthreads_lib)
    foreach(_mcp_libdir IN ITEMS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib)
      if(EXISTS "${_mcp_libdir}")
        file(GLOB _mcp_pthread_candidates "${_mcp_libdir}/libevent_pthreads*.so*")
        if(_mcp_pthread_candidates)
          list(SORT _mcp_pthread_candidates)
          list(GET _mcp_pthread_candidates 0 _mcp_libevent_pthreads_lib)
          break()
        endif()
      endif()
    endforeach()
  endif()
  set(${out_var} "${_mcp_libevent_pthreads_lib}" PARENT_SCOPE)
endfunction()

function(_mcp_setup_system_libevent_targets include_dir main_lib pthread_lib)
  if(NOT TARGET event)
    add_library(event SHARED IMPORTED GLOBAL)
    set_target_properties(event PROPERTIES
      IMPORTED_LOCATION "${main_lib}"
      INTERFACE_INCLUDE_DIRECTORIES "${include_dir}"
    )
  endif()

  if(NOT TARGET libevent)
    add_library(libevent SHARED IMPORTED GLOBAL)
    set_target_properties(libevent PROPERTIES
      IMPORTED_LOCATION "${main_lib}"
      INTERFACE_INCLUDE_DIRECTORIES "${include_dir}"
    )
  endif()

  if(NOT TARGET libevent_headers)
    add_library(libevent_headers INTERFACE)
    target_include_directories(libevent_headers INTERFACE "${include_dir}")
  endif()

  if(pthread_lib AND NOT TARGET event_pthreads)
    add_library(event_pthreads SHARED IMPORTED GLOBAL)
    set_target_properties(event_pthreads PROPERTIES
      IMPORTED_LOCATION "${pthread_lib}"
      INTERFACE_INCLUDE_DIRECTORIES "${include_dir}"
    )
  endif()
endfunction()

function(_mcp_try_use_system_libevent out_var)
  set(_mcp_found FALSE)
  find_path(_mcp_libevent_include_dir
    NAMES event.h
    PATHS /usr/include /usr/local/include
  )
  find_library(_mcp_libevent_lib
    NAMES event event-2.1 libevent
    PATHS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib
  )
  _mcp_find_libevent_pthreads_library(_mcp_libevent_pthreads_lib)

  if(_mcp_libevent_include_dir AND _mcp_libevent_lib AND _mcp_libevent_pthreads_lib)
    set(_mcp_found TRUE)
    message(STATUS "Using system libevent: include=${_mcp_libevent_include_dir}, lib=${_mcp_libevent_lib}, pthread_lib=${_mcp_libevent_pthreads_lib}")
    _mcp_setup_system_libevent_targets(
      "${_mcp_libevent_include_dir}"
      "${_mcp_libevent_lib}"
      "${_mcp_libevent_pthreads_lib}"
    )
  else()
    message(STATUS "system libevent probe failed: include=${_mcp_libevent_include_dir}, lib=${_mcp_libevent_lib}, pthread_lib=${_mcp_libevent_pthreads_lib}")
  endif()

  set(${out_var} "${_mcp_found}" PARENT_SCOPE)
endfunction()

function(_mcp_fetch_git_package)
  set(oneValueArgs NAME GIT_REPO GIT_TAG)
  set(multiValueArgs OPTIONS)
  cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(_src_dir "${MCP_SDK_ROOT}/third_party/${ARG_NAME}-src")

  if(EXISTS "${_src_dir}/CMakeLists.txt")
    message(STATUS "[${ARG_NAME}] Using existing source at ${_src_dir}")
    FetchContent_Declare(${ARG_NAME} SOURCE_DIR "${_src_dir}")
  else()
    message(STATUS "[${ARG_NAME}] Fetching from ${ARG_GIT_REPO} (${ARG_GIT_TAG})")
    FetchContent_Declare(
      ${ARG_NAME}
      GIT_REPOSITORY ${ARG_GIT_REPO}
      GIT_TAG ${ARG_GIT_TAG}
      SOURCE_DIR "${_src_dir}"
    )
  endif()

  if(ARG_OPTIONS)
    list(LENGTH ARG_OPTIONS _opt_len)
    math(EXPR _last_idx "${_opt_len} - 1")
    foreach(i RANGE 0 ${_last_idx} 2)
      list(GET ARG_OPTIONS ${i} _opt_name)
      math(EXPR _val_idx "${i} + 1")
      list(GET ARG_OPTIONS ${_val_idx} _opt_value)
      set(${_opt_name} ${_opt_value} CACHE BOOL "Auto-configured by third_party.cmake" FORCE)
    endforeach()
  endif()

  FetchContent_MakeAvailable(${ARG_NAME})
  message(STATUS "${ARG_NAME} source dir: ${${ARG_NAME}_SOURCE_DIR}")

  if(NOT "${ARG_NAME}" STREQUAL "http_parser")
    target_include_directories(third_party_headers INTERFACE
      "${${ARG_NAME}_BINARY_DIR}/include/"
      "${${ARG_NAME}_SOURCE_DIR}/include/"
    )
  endif()
endfunction()

function(fetch_or_find_package)
  set(oneValueArgs NAME GIT_REPO GIT_TAG SYSTEM_PACKAGE_NAME TARGET_NAME)
  set(multiValueArgs OPTIONS)
  cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARG_NAME OR NOT ARG_GIT_REPO OR NOT ARG_GIT_TAG)
    message(FATAL_ERROR "fetch_or_find_package: NAME, GIT_REPO and GIT_TAG are required")
  endif()

  if(NOT ARG_SYSTEM_PACKAGE_NAME)
    set(ARG_SYSTEM_PACKAGE_NAME ${ARG_NAME})
  endif()

  string(TOUPPER ${ARG_NAME} PACKAGE_UPPER)
  string(REPLACE "-" "_" PACKAGE_UPPER ${PACKAGE_UPPER})

  set(OPTION_VAR "MCP_USE_SYSTEM_${PACKAGE_UPPER}")
  option(${OPTION_VAR} "Try to use system ${ARG_NAME} before fetching" ON)

  set(PACKAGE_FOUND FALSE)
  if(${OPTION_VAR})
    find_package(${ARG_SYSTEM_PACKAGE_NAME} QUIET)
    if(${ARG_SYSTEM_PACKAGE_NAME}_FOUND)
      set(PACKAGE_FOUND TRUE)
      message(STATUS "Using system ${ARG_NAME} (find_package)")
    endif()

    if(NOT PACKAGE_FOUND)
      find_package(PkgConfig QUIET)
      if(PKG_CONFIG_FOUND)
        set(_mcp_pkg_names "${ARG_SYSTEM_PACKAGE_NAME}")
        string(TOLOWER "${ARG_SYSTEM_PACKAGE_NAME}" _mcp_pkg_lower)
        list(APPEND _mcp_pkg_names "${_mcp_pkg_lower}")
        list(REMOVE_DUPLICATES _mcp_pkg_names)

        foreach(_mcp_pkg_name IN LISTS _mcp_pkg_names)
          pkg_check_modules(_mcp_pkg QUIET IMPORTED_TARGET "${_mcp_pkg_name}")
          if(_mcp_pkg_FOUND AND _mcp_pkg_INCLUDE_DIRS AND _mcp_pkg_LIBRARIES
              AND NOT "${_mcp_pkg_INCLUDE_DIRS}" STREQUAL "NOTFOUND"
              AND NOT "${_mcp_pkg_LIBRARIES}" STREQUAL "NOTFOUND")
            set(PACKAGE_FOUND TRUE)
            message(STATUS "Using system ${ARG_NAME} (pkg-config: ${_mcp_pkg_name})")

            if(NOT TARGET "${ARG_SYSTEM_PACKAGE_NAME}::${ARG_SYSTEM_PACKAGE_NAME}")
              add_library("${ARG_SYSTEM_PACKAGE_NAME}::${ARG_SYSTEM_PACKAGE_NAME}" INTERFACE IMPORTED)
              set_target_properties("${ARG_SYSTEM_PACKAGE_NAME}::${ARG_SYSTEM_PACKAGE_NAME}" PROPERTIES
                INTERFACE_LINK_LIBRARIES "PkgConfig::_mcp_pkg"
              )
            endif()

            if("${ARG_SYSTEM_PACKAGE_NAME}" STREQUAL "nlohmann_json"
                AND NOT TARGET nlohmann_json::nlohmann_json)
              add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
              set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
                INTERFACE_LINK_LIBRARIES "PkgConfig::_mcp_pkg"
              )
            endif()
          endif()
        endforeach()
      endif()
    endif()
  endif()

  if(NOT PACKAGE_FOUND)
    _mcp_fetch_git_package(
      NAME ${ARG_NAME}
      GIT_REPO ${ARG_GIT_REPO}
      GIT_TAG ${ARG_GIT_TAG}
      OPTIONS ${ARG_OPTIONS}
    )

    if(ARG_TARGET_NAME AND NOT TARGET ${ARG_NAME} AND TARGET ${ARG_TARGET_NAME})
      add_library(${ARG_NAME} ALIAS ${ARG_TARGET_NAME})
      message(STATUS "Created alias ${ARG_NAME} -> ${ARG_TARGET_NAME}")
    endif()
  endif()
endfunction()

# libevent: filesystem probe first (openEuler/RHEL), then fetch
option(MCP_USE_SYSTEM_LIBEVENT "Use system libevent before fetching" ON)
set(_mcp_use_system_libevent FALSE)
if(MCP_USE_SYSTEM_LIBEVENT)
  _mcp_try_use_system_libevent(_mcp_use_system_libevent)
endif()
if(NOT _mcp_use_system_libevent)
  _mcp_fetch_git_package(
    NAME libevent
    GIT_REPO https://github.com/libevent/libevent.git
    GIT_TAG release-2.1.12-stable
    OPTIONS
      EVENT__LIBRARY_TYPE SHARED
      EVENT__DISABLE_OPENSSL ON
      EVENT__DISABLE_TESTS ON
      EVENT__DISABLE_BENCHMARK ON
  )
endif()

fetch_or_find_package(
  NAME nlohmann_json
  GIT_REPO https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
  OPTIONS JSON_BuildTests OFF JSON_Install OFF
)

list(APPEND CMAKE_PREFIX_PATH "/usr/local")
fetch_or_find_package(
  NAME nlohmann_json_schema_validator
  GIT_REPO https://github.com/pboettch/json-schema-validator.git
  GIT_TAG 2.3.0
  SYSTEM_PACKAGE_NAME nlohmann_json_schema_validator
  TARGET_NAME nlohmann_json_schema_validator
)

if(NOT TARGET nlohmann_json_schema_validator)
  if(TARGET nlohmann_json_schema_validator::nlohmann_json_schema_validator)
    add_library(nlohmann_json_schema_validator ALIAS nlohmann_json_schema_validator::nlohmann_json_schema_validator)
  elseif(TARGET json-schema-validator)
    add_library(nlohmann_json_schema_validator ALIAS json-schema-validator)
  elseif(TARGET nlohmann_json_schema_validator::json-schema-validator)
    add_library(nlohmann_json_schema_validator ALIAS nlohmann_json_schema_validator::json-schema-validator)
  else()
    find_path(_mcp_json_schema_validator_include_dir
      NAMES json-schema.hpp
      PATHS /usr/include /usr/local/include
      PATH_SUFFIXES nlohmann
    )
    find_library(_mcp_json_schema_validator_lib
      NAMES nlohmann_json_schema_validator json-schema-validator
      PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/local/lib64
    )
    if(_mcp_json_schema_validator_include_dir AND _mcp_json_schema_validator_lib)
      add_library(nlohmann_json_schema_validator SHARED IMPORTED GLOBAL)
      set_target_properties(nlohmann_json_schema_validator PROPERTIES
        IMPORTED_LOCATION "${_mcp_json_schema_validator_lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${_mcp_json_schema_validator_include_dir}"
      )
    endif()
  endif()
endif()

if(NOT TARGET nlohmann_json_schema_validator)
  message(FATAL_ERROR "json-schema-validator target not found")
endif()

set_target_properties(nlohmann_json_schema_validator PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(third_party_headers INTERFACE nlohmann_json_schema_validator)

find_path(_mcp_http_parser_include_dir NAMES http_parser.h)
find_library(_mcp_http_parser_lib NAMES http_parser)

if(_mcp_http_parser_include_dir AND _mcp_http_parser_lib)
  message(STATUS "Using system http_parser: include=${_mcp_http_parser_include_dir}, lib=${_mcp_http_parser_lib}")
  if(NOT TARGET http_parser)
    add_library(http_parser STATIC IMPORTED GLOBAL)
    set_target_properties(http_parser PROPERTIES
      IMPORTED_LOCATION "${_mcp_http_parser_lib}"
      INTERFACE_INCLUDE_DIRECTORIES "${_mcp_http_parser_include_dir}"
    )
  endif()
else()
  if(NOT EXISTS "${FETCHCONTENT_BASE_DIR}/http_parser-src/http_parser.c")
    FetchContent_Declare(
      http_parser
      GIT_REPOSITORY https://github.com/nodejs/http-parser.git
      GIT_TAG v2.9.4
    )
    FetchContent_MakeAvailable(http_parser)
  endif()
  if(NOT TARGET http_parser)
    add_library(http_parser STATIC ${FETCHCONTENT_BASE_DIR}/http_parser-src/http_parser.c)
    set_target_properties(http_parser PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(http_parser PUBLIC ${FETCHCONTENT_BASE_DIR}/http_parser-src/)
  endif()
endif()

if(NOT TARGET http_parser_headers)
  add_library(http_parser_headers INTERFACE)
  if(_mcp_http_parser_include_dir)
    target_include_directories(http_parser_headers INTERFACE "${_mcp_http_parser_include_dir}")
  else()
    target_include_directories(http_parser_headers INTERFACE "${FETCHCONTENT_BASE_DIR}/http_parser-src/")
  endif()
endif()
