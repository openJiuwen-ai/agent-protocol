include_directories(${CMAKE_SOURCE_DIR}/include)

if(A2A_BUILD_CLIENT)
    add_executable(helloworld_client helloworld_client.cpp)
    set_property(TARGET helloworld_client PROPERTY C_STANDARD 11)
    target_link_libraries(helloworld_client PRIVATE a2a)

    add_executable(streaming_client streaming_client.cpp)
    set_property(TARGET streaming_client PROPERTY C_STANDARD 11)
    target_link_libraries(streaming_client PRIVATE a2a)

    # 超时功能打桩验证：自带"只 accept、永不回包"的桩服务，无需真实 server
    add_executable(timeout_client timeout_client.cpp)
    set_property(TARGET timeout_client PROPERTY C_STANDARD 11)
    target_link_libraries(timeout_client PRIVATE a2a)
endif()

if(A2A_BUILD_SERVER)
    add_executable(helloworld_server helloworld_server.cpp)
    set_property(TARGET helloworld_server PROPERTY C_STANDARD 11)
    target_link_libraries(helloworld_server PRIVATE a2a pthread)

    add_executable(streaming_server streaming_server.cpp)
    set_property(TARGET streaming_server PROPERTY C_STANDARD 11)
    target_link_libraries(streaming_server PRIVATE a2a pthread)
endif()
