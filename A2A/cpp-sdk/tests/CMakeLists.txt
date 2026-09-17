include(FetchContent)
include(GoogleTest)

message(STATUS "Configuring googletest for unit tests")
find_package(GTest QUIET)

set(_a2a_has_gtest_target OFF)
set(_a2a_has_gtest_main_target OFF)
if(TARGET GTest::gtest)
    set(_a2a_has_gtest_target ON)
endif()
if(TARGET GTest::gtest_main)
    set(_a2a_has_gtest_main_target ON)
endif()

if(NOT TARGET GTest::gtest AND NOT TARGET GTest::gtest_main)
    message(STATUS "GTest not found on system; fetching googletest from GitHub")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.16.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
else()
    message(STATUS "Using system googletest")
endif()

add_subdirectory(ut)
