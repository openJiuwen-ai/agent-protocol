# Centralized third-party dependencies (FetchContent) for mcp_cpp
# This file will try to find system-provided packages first, then fall back to FetchContent.

include(FetchContent)

# Set FetchContent base directory
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "" FORCE)

add_library(third_party_headers INTERFACE)

# Generic function to fetch or find third-party packages
# Usage: fetch_or_find_package(
#   NAME <package_name>
#   GIT_REPO <git_repository_url>
#   GIT_TAG <git_tag>
#   [SYSTEM_PACKAGE_NAME <name>]  # Optional, defaults to NAME
#   [OPTIONS <var1> <val1> <var2> <val2> ...]  # Optional build options
# )
function(fetch_or_find_package)
  set(options "")
  set(oneValueArgs NAME GIT_REPO GIT_TAG SYSTEM_PACKAGE_NAME)
  set(multiValueArgs OPTIONS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Validate required arguments
  if(NOT ARG_NAME)
    message(FATAL_ERROR "fetch_or_find_package: NAME is required")
  endif()
  if(NOT ARG_GIT_REPO)
    message(FATAL_ERROR "fetch_or_find_package: GIT_REPO is required")
  endif()
  if(NOT ARG_GIT_TAG)
    message(FATAL_ERROR "fetch_or_find_package: GIT_TAG is required")
  endif()

  # Use NAME as system package name if not specified
  if(NOT ARG_SYSTEM_PACKAGE_NAME)
    set(ARG_SYSTEM_PACKAGE_NAME ${ARG_NAME})
  endif()

  # Convert package name to uppercase for option variable
  string(TOUPPER ${ARG_NAME} PACKAGE_UPPER)
  string(REPLACE "-" "_" PACKAGE_UPPER ${PACKAGE_UPPER})

  # Create option to control whether to use system package
  set(OPTION_VAR "MCP_USE_SYSTEM_${PACKAGE_UPPER}")
  option(${OPTION_VAR} "Try to use system ${ARG_NAME} before fetching" ON)

  # Try to find system package first
  set(PACKAGE_FOUND FALSE)
  if(${OPTION_VAR})
    message(STATUS "try to find system ${ARG_SYSTEM_PACKAGE_NAME}")
    # 1 Try find _package first
    find_package(${ARG_SYSTEM_PACKAGE_NAME} QUIET)
    if (${ARG_SYSTEM_PACKAGE_NAME}_FOUND)
      set(PACKAGE_FOUND TRUE)
      message(STATUS "find package success")
    endif()

    # 2 Try pkgconfig to find
    if (NOT PACKAGE_FOUND)
      message(STATUS "find package failed")
      find_package(PkgConfig QUIET)
      if(PKG_CONFIG_FOUND)
        set(_mcp_pkg_names "${ARG_SYSTEM_PACKAGE_NAME}")
        string(TOLOWER "${ARG_SYSTEM_PACKAGE_NAME}" _mcp_pkg_lower)
        list(APPEND _mcp_pkg_names "${_mcp_pkg_lower}")
        list(REMOVE_DUPLICATES _mcp_pkg_names)

        foreach(_mcp_pkg_name IN LISTS _mcp_pkg_names)
          pkg_check_modules(_mcp_pkg QUIET IMPORTED_TARGET "${_mcp_pkg_name}")
          if(_mcp_pkg_FOUND AND _mcp_pkg_INCLUDE_DIRS AND _mcp_pkg_LIBRARIES)
            if (NOT "${_mcp_pkg_INCLUDE_DIRS}" STREQUAL "NOTFOUND" AND NOT "${_mcp_pkg_LIBRARIES}" STREQUAL "NOTFOUND")
              set(PACKAGE_FOUND TRUE)
              message(STATUS "pkgconfig success")

              message(STATUS "_mcp_pkg_INCLUDE_DIRS": ${_mcp_pkg_INCLUDE_DIRS})
              message(STATUS "_mcp_pkg_LIBRARIES": ${_mcp_pkg_LIBRARIES})

              if(NOT TARGET "${ARG_SYSTEM_PACKAGE_NAME}::${ARG_SYSTEM_PACKAGE_NAME}")
                add_library("${ARG_SYSTEM_PACKAGE_NAME}::${ARG_SYSTEM_PACKAGE_NAME}" INTERFACE IMPORTED)
                set_target_properties("${ARG_SYSTEM_PACKAGE_NAME}::${ARG_SYSTEM_PACKAGE_NAME}" PROPERTIES
                  INTERFACE_LINK_LIBRARIES "PkgConfig::_mcp_pkg"
                )
              endif()

              if ("${ARG_SYSTEM_PACKAGE_NAME}" STREQUAL "libevent")
                if(NOT TARGET event)
                  add_library(event INTERFACE IMPORTED)
                  set_target_properties(event PROPERTIES
                    INTERFACE_LINK_LIBRARIES "PkgConfig::_mcp_pkg"
                  )
                endif()
              elseif("${ARG_SYSTEM_PACKAGE_NAME}" STREQUAL "nlohmann_json")
                if(NOT TARGET nlohmann_json::nlohmann_json)
                  add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
                  set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
                    INTERFACE_LINK_LIBRARIES "PkgConfig::_mcp_pkg"
                  )
                endif()
              endif()
            endif()
          endif()

        endforeach()
      endif()
    endif()

    # 3 find_path and find_library finally
    if (NOT PACKAGE_FOUND)
      message(STATUS "pkgconfig fail")

      if ("${ARG_NAME}" STREQUAL "libevent")
        message(STATUS "try find_path and find_library")

        find_path(_mcp_libevent_include_dir
          NAMES event.h
          PATHS /usr/include /usr/local/include
        )
        find_library(_mcp_libevent_lib
          NAMES event
          PATHS /usr/lib /usr/lib64
        )
        find_library(_mcp_libevent_pthreads_lib
          NAMES event_pthreads
          PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64
        )

        if(_mcp_libevent_include_dir AND _mcp_libevent_lib AND _mcp_libevent_pthreads_lib)
          set(PACKAGE_FOUND TRUE)
          message(STATUS "Using system libevent: include=${_mcp_libevent_include_dir}, lib=${_mcp_libevent_lib}, pthread_lib=${_mcp_libevent_pthreads_lib}")

          if(NOT TARGET libevent)
              add_library(libevent SHARED IMPORTED)
              set_target_properties(libevent PROPERTIES
              IMPORTED_LOCATION "${_mcp_libevent_lib}"
              INTERFACE_INCLUDE_DIRECTORIES "${_mcp_libevent_include_dir}"
          )
          endif()

          if(NOT TARGET libevent_headers)
            add_library(libevent_headers INTERFACE)
            target_include_directories(libevent_headers
                INTERFACE
                    "${_mcp_libevent_include_dir}"
            )
          endif()

          if(NOT TARGET libevent_pthreads)
            add_library(libevent_pthreads SHARED IMPORTED)
            set_target_properties(libevent_pthreads PROPERTIES
              IMPORTED_LOCATION "${_mcp_libevent_pthreads_lib}"
              INTERFACE_INCLUDE_DIRECTORIES "${_mcp_libevent_include_dir}"
            )
          endif()

        else()
          message(STATUS "fail to using system libevent: include=${_mcp_libevent_include_dir}, lib=${_mcp_libevent_lib}, pthread_lib=${_mcp_libevent_pthrad_lib}")
        endif()

        if (NOT PACKAGE_FOUND)
          message(STATUS "find_path and find_library fail")
        endif()

      endif()

    endif()

  endif()

  set(${PACKAGE_UPPER}_SRC_DIR "${CMAKE_SOURCE_DIR}/third_party/${ARG_SYSTEM_PACKAGE_NAME}-src")
  # If not found, fetch from repository
  if(NOT PACKAGE_FOUND)
    message(STATUS "find ${${PACKAGE_UPPER}_SRC_DIR}/CMakeLists.txt")
    if(EXISTS "${${PACKAGE_UPPER}_SRC_DIR}/CMakeLists.txt")
      message(STATUS "[${ARG_NAME}] Found existing source at ${${PACKAGE_UPPER}_SRC_DIR}, using it")
      FetchContent_Declare(
          ${ARG_NAME}
          SOURCE_DIR "${${PACKAGE_UPPER}_SRC_DIR}"
      )
    else()
      message(STATUS "[${ARG_NAME}] Source not found, will fetch from repository")
      # download
      FetchContent_Declare(
        ${ARG_NAME}
        GIT_REPOSITORY ${ARG_GIT_REPO}
        GIT_TAG ${ARG_GIT_TAG}
      )
    endif()

    # Apply custom build options if provided
    if(ARG_OPTIONS)
      list(LENGTH ARG_OPTIONS options_length)
      math(EXPR options_pairs "${options_length} / 2")
      math(EXPR last_index "${options_length} - 1")

      foreach(i RANGE 0 ${last_index} 2)
        list(GET ARG_OPTIONS ${i} opt_name)
        math(EXPR val_index "${i} + 1")
        list(GET ARG_OPTIONS ${val_index} opt_value)
        set(${opt_name} ${opt_value} CACHE BOOL "Auto-configured by fetch_or_find_package" FORCE)
      endforeach()

    endif()

    FetchContent_MakeAvailable(${ARG_NAME})

    # Print source and binary directories
    message(STATUS "${ARG_NAME} source dir: ${${ARG_NAME}_SOURCE_DIR}")
    message(STATUS "${ARG_NAME} binary dir: ${${ARG_NAME}_BINARY_DIR}")

    if (${ARG_NAME} STREQUAL "http_parser")
      target_include_directories(third_party_headers INTERFACE
        "${${ARG_NAME}_BINARY_DIR}/"
        "${${ARG_NAME}_SOURCE_DIR}/"
      )
    else()
      target_include_directories(third_party_headers INTERFACE
        "${${ARG_NAME}_BINARY_DIR}/include/"
        "${${ARG_NAME}_SOURCE_DIR}/include/"
      )
    endif()

  else()
    message(STATUS "Using system ${ARG_NAME}")
  endif()
endfunction()

# Fetch libevent
fetch_or_find_package(
  NAME libevent
  GIT_REPO https://github.com/libevent/libevent.git
  GIT_TAG release-2.1.12-stable
  SYSTEM_PACKAGE_NAME Libevent
  OPTIONS
    EVENT__LIBRARY_TYPE SHARED
    EVENT__DISABLE_OPENSSL ON
    EVENT__DISABLE_TESTS ON
    EVENT__DISABLE_BENCHMARK ON
)

# Fetch nlohmann/json (header-only library)
fetch_or_find_package(
  NAME nlohmann_json
  GIT_REPO https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
  OPTIONS
    JSON_BuildTests OFF
    JSON_Install OFF
)

find_path(_mcp_http_parser_include_dir
    NAMES http_parser.h
)
find_library(_mcp_http_parser_lib
    NAMES http_parser
)

if(_mcp_http_parser_include_dir AND _mcp_http_parser_lib)
    message(STATUS "Using system http_parser: include=${_mcp_http_parser_include_dir}, lib=${_mcp_http_parser_lib}")
    if(NOT TARGET http_parser)
        add_library(http_parser STATIC IMPORTED)
        set_target_properties(http_parser PROPERTIES
            IMPORTED_LOCATION "${_mcp_http_parser_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_mcp_http_parser_include_dir}"
        )
    endif()

    if(NOT TARGET http_parser_headers)
        add_library(http_parser_headers INTERFACE)
        target_include_directories(http_parser_headers
            INTERFACE
                "${_mcp_http_parser_include_dir}"
        )
    endif()
else()
    if (NOT EXISTS "${FETCHCONTENT_BASE_DIR}/http_parser-src/http_parser.c")
        message(STATUS "download http_parser")
        FetchContent_Declare(
            http_parser
            GIT_REPOSITORY https://github.com/nodejs/http-parser.git
            GIT_TAG        v2.9.4
        )
        # Fetch http_parser
        FetchContent_MakeAvailable(http_parser)
        message(STATUS "finish download http_parser")
    endif()

    if(NOT TARGET http_parser)
        add_library(http_parser
            STATIC
                ${FETCHCONTENT_BASE_DIR}/http_parser-src/http_parser.c
        )

        set_target_properties(http_parser PROPERTIES POSITION_INDEPENDENT_CODE ON)

        target_include_directories(http_parser
            PUBLIC
                ${FETCHCONTENT_BASE_DIR}/http_parser-src/
        )
    endif()

    if(NOT TARGET http_parser_headers)
        add_library(http_parser_headers INTERFACE)
        target_include_directories(http_parser_headers
            INTERFACE
                ${FETCHCONTENT_BASE_DIR}/http_parser-src/
        )
    endif()
endif()
