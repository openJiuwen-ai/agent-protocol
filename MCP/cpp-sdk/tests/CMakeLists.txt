include(FetchContent)
include(GoogleTest)

message(STATUS "use googletest")
find_package(GTest QUIET)
message(STATUS "GTest_FOUND=${GTest_FOUND}, GTEST_FOUND=${GTEST_FOUND}")
set(_mcp_has_gtest_target OFF)
set(_mcp_has_gtest_main_target OFF)
if(TARGET GTest::gtest)
  set(_mcp_has_gtest_target ON)
endif()
if(TARGET GTest::gtest_main)
  set(_mcp_has_gtest_main_target ON)
endif()
message(STATUS "GTest targets: gtest=${_mcp_has_gtest_target}, gtest_main=${_mcp_has_gtest_main_target}")
if(NOT TARGET GTest::gtest AND NOT TARGET GTest::gtest_main)
  message(STATUS "download googletest")
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
  )

  # Ensure consistent runtime behavior on all platforms.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)
  message(STATUS "finish download googletest")
else()
  message(STATUS "use system googletest: GTest::gtest=${_mcp_has_gtest_target}, GTest::gtest_main=${_mcp_has_gtest_main_target}")
endif()

add_subdirectory(ut)
