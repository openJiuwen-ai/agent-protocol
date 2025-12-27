
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(helloworld_client helloworld_client.cpp)
set_property(TARGET helloworld_client PROPERTY C_STANDARD 11)
target_link_libraries(helloworld_client PRIVATE a2a_cpp)

add_executable(streaming_client streaming_client.cpp)
set_property(TARGET streaming_client PROPERTY C_STANDARD 11)
target_link_libraries(streaming_client PRIVATE a2a_cpp)

add_executable(helloworld_server helloworld_server.cpp)
set_property(TARGET helloworld_server PROPERTY C_STANDARD 11)
target_link_libraries(helloworld_server PRIVATE a2a_cpp)

add_executable(streaming_server streaming_server.cpp)
set_property(TARGET streaming_server PROPERTY C_STANDARD 11)
target_link_libraries(streaming_server PRIVATE a2a_cpp)
