file(GLOB_RECURSE A2A_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/event/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/log/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp"
)

file(GLOB_RECURSE A2A_CLIENT_SOURCES "${CMAKE_SOURCE_DIR}/src/client/*.cpp")
file(GLOB_RECURSE A2A_SERVER_SOURCES "${CMAKE_SOURCE_DIR}/src/server/*.cpp")
file(GLOB_RECURSE A2A_EVENT_SOURCES "${CMAKE_SOURCE_DIR}/src/event/*.cpp")
file(GLOB_RECURSE A2A_LOG_SOURCES "${CMAKE_SOURCE_DIR}/src/log/*.cpp")
file(GLOB_RECURSE A2A_SHARED_SOURCES "${CMAKE_SOURCE_DIR}/src/shared/*.cpp")
file(GLOB_RECURSE A2A_TRANSPORT_SOURCES "${CMAKE_SOURCE_DIR}/src/transport/*.cpp")
file(GLOB_RECURSE A2A_UTILS_SOURCES "${CMAKE_SOURCE_DIR}/src/utils/*.cpp")

add_executable(a2a_ut_test
    ${A2A_TEST_SOURCES}
    ${A2A_CLIENT_SOURCES}
    ${A2A_SERVER_SOURCES}
    ${A2A_EVENT_SOURCES}
    ${A2A_LOG_SOURCES}
    ${A2A_SHARED_SOURCES}
    ${A2A_TRANSPORT_SOURCES}
    ${A2A_UTILS_SOURCES}
)

target_include_directories(a2a_ut_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/ut/utils
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/client
        ${CMAKE_SOURCE_DIR}/src/client/connection
        ${CMAKE_SOURCE_DIR}/src/server
        ${CMAKE_SOURCE_DIR}/src/server/net
        ${CMAKE_SOURCE_DIR}/src/server/tasks
        ${CMAKE_SOURCE_DIR}/src/event
        ${CMAKE_SOURCE_DIR}/src/log
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/transport
        ${CMAKE_SOURCE_DIR}/src/utils
)

set(_a2a_ut_libs
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    http_parser
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)

if(TARGET CURL::libcurl)
    list(APPEND _a2a_ut_libs CURL::libcurl)
elseif(TARGET libcurl)
    list(APPEND _a2a_ut_libs libcurl)
endif()

if(TARGET event)
    list(APPEND _a2a_ut_libs event)
elseif(TARGET libevent)
    list(APPEND _a2a_ut_libs libevent)
endif()

if(TARGET event_pthreads)
    list(APPEND _a2a_ut_libs event_pthreads)
elseif(TARGET libevent_pthreads)
    list(APPEND _a2a_ut_libs libevent_pthreads)
else()
    find_library(_a2a_event_pthreads_lib NAMES event_pthreads libevent_pthreads)
    if(_a2a_event_pthreads_lib)
        list(APPEND _a2a_ut_libs ${_a2a_event_pthreads_lib})
    endif()
endif()

find_package(Threads QUIET)
if(Threads_FOUND)
    list(APPEND _a2a_ut_libs Threads::Threads)
endif()

target_link_libraries(a2a_ut_test PRIVATE ${_a2a_ut_libs})

target_compile_features(a2a_ut_test PRIVATE cxx_std_17)

if(A2A_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(a2a_ut_test PRIVATE --coverage -O0 -g)
        target_link_options(a2a_ut_test PRIVATE --coverage)
    else()
        message(WARNING "Coverage not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

gtest_discover_tests(a2a_ut_test DISCOVERY_MODE PRE_TEST)
