file(GLOB test_sources "*.cpp")

foreach(test_source ${test_sources})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${test_name} mcp_shared mcp_log third_party_headers gtest gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
