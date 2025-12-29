# Centralized third-party dependencies (FetchContent) for a2a_cpp
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
  set(OPTION_VAR "A2A_USE_SYSTEM_${PACKAGE_UPPER}")
  option(${OPTION_VAR} "Try to use system ${ARG_NAME} before fetching" ON)

  # Try to find system package first
  set(PACKAGE_FOUND FALSE)
  if(${OPTION_VAR})
    find_package(${ARG_SYSTEM_PACKAGE_NAME} QUIET)
    if (${ARG_SYSTEM_PACKAGE_NAME}_FOUND)
      message(STATUS "${ARG_SYSTEM_PACKAGE_NAME} found")
      set(PACKAGE_FOUND TRUE)
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
  else()
    message(STATUS "Using system ${ARG_NAME}")
  endif()
  if (${ARG_NAME} STREQUAL "cpp-httplib")
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
endfunction()

# Fetch cpp_httplib
fetch_or_find_package(
  NAME cpp-httplib
  GIT_REPO https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.7
)

# Fetch nlohmann/json (header-only library)
fetch_or_find_package(
  NAME nlohmann_json
  GIT_REPO https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  OPTIONS
    JSON_BuildTests OFF
    JSON_Install OFF
)
