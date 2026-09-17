# Shared CMake helpers for MCP examples built outside the main SDK tree.

if(NOT DEFINED MCP_SDK_ROOT)
  message(FATAL_ERROR "MCP_SDK_ROOT must be set before including ExampleCommon.cmake")
endif()

get_filename_component(MCP_SDK_ROOT "${MCP_SDK_ROOT}" ABSOLUTE)

set(MCP_EXAMPLE_LIB "${MCP_SDK_ROOT}/output/lib/libmcp.so")
if(NOT EXISTS "${MCP_EXAMPLE_LIB}")
  message(FATAL_ERROR
    "libmcp.so not found at ${MCP_EXAMPLE_LIB}. Build the SDK first: ./scripts/build.sh"
  )
endif()

set(MCP_EXAMPLE_NLOHMANN_TARGET "")

# 1) Prefer system nlohmann_json (CMake package or headers under /usr/include)
find_package(nlohmann_json 3.11.2 QUIET)
if(TARGET nlohmann_json::nlohmann_json)
  set(MCP_EXAMPLE_NLOHMANN_TARGET nlohmann_json::nlohmann_json)
  message(STATUS "MCP example: using system nlohmann_json (find_package)")
else()
  find_path(MCP_NLOHMANN_SYSTEM_INCLUDE
    NAMES nlohmann/json.hpp
    PATHS /usr/include /usr/local/include
  )
  if(MCP_NLOHMANN_SYSTEM_INCLUDE)
    if(NOT TARGET mcp_example_nlohmann_json)
      add_library(mcp_example_nlohmann_json INTERFACE)
      target_include_directories(mcp_example_nlohmann_json INTERFACE "${MCP_NLOHMANN_SYSTEM_INCLUDE}")
    endif()
    set(MCP_EXAMPLE_NLOHMANN_TARGET mcp_example_nlohmann_json)
    message(STATUS "MCP example: using system nlohmann_json headers at ${MCP_NLOHMANN_SYSTEM_INCLUDE}")
  endif()
endif()

# 2) Fall back to headers fetched by the main SDK build
if(NOT MCP_EXAMPLE_NLOHMANN_TARGET)
  set(MCP_NLOHMANN_VENDOR_INCLUDE "${MCP_SDK_ROOT}/third_party/nlohmann_json-src/include")
  if(EXISTS "${MCP_NLOHMANN_VENDOR_INCLUDE}/nlohmann/json.hpp")
    if(NOT TARGET mcp_example_nlohmann_json)
      add_library(mcp_example_nlohmann_json INTERFACE)
      target_include_directories(mcp_example_nlohmann_json INTERFACE "${MCP_NLOHMANN_VENDOR_INCLUDE}")
    endif()
    set(MCP_EXAMPLE_NLOHMANN_TARGET mcp_example_nlohmann_json)
    message(STATUS "MCP example: using third_party nlohmann_json at ${MCP_NLOHMANN_VENDOR_INCLUDE}")
  else()
    message(FATAL_ERROR
      "nlohmann_json not found. Install a system package (e.g. nlohmann-json-devel) "
      "or build the SDK first so ${MCP_NLOHMANN_VENDOR_INCLUDE} is populated: ./scripts/build.sh"
    )
  endif()
endif()

function(mcp_link_example_dependencies target)
  target_include_directories(${target} PRIVATE "${MCP_SDK_ROOT}/include/mcp")
  target_link_libraries(${target} PRIVATE "${MCP_EXAMPLE_LIB}" ${MCP_EXAMPLE_NLOHMANN_TARGET})
endfunction()
