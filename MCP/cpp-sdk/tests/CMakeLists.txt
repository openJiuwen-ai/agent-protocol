include(FetchContent)
include(GoogleTest)

if(NOT TARGET GTest::gtest AND NOT TARGET GTest::gtest_main)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
  )

  # Ensure consistent runtime behavior on all platforms.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(ut)