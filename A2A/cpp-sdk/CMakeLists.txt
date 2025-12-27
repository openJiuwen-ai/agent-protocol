cmake_minimum_required(VERSION 3.16)
project(a2a_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建类型设置
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项配置
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -D_FORTIFY_SOURCE=2 -DDEBUG -rdynamic")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -O2 -D_FORTIFY_SOURCE=2")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
endif()

# 通用编译选项
set(CMAKE_FLAGS_PUBLIC " -Wall -Werror -Wfloat-equal -Wtrampolines -g -rdynamic \
-fno-strict-aliasing -fstack-protector-strong -fPIE -pie -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_FLAGS_PUBLIC}")
set(CMAKE_SKIP_RPATH TRUE)

# 链接选项
add_link_options(-Wl,-z,noexecstack,-z,relro,-z,now)

# 构建选项
option(BUILD_SHARED_LIBS "Build shared librarier" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_EXAMPLES "Build example programs" OFF)
option(BUILD_OHOS "Build ohos platform" OFF)
option(A2A_HTTP "Enable HTTP transport and demo server" ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/lib)

# 指定curl的库路径
find_package(CURL REQUIRED)

# 指定curl的库路径
find_package(OpenSSL REQUIRED)

set(nlohmann_json_DIR "${LIB_PATH}/share/cmake/nlohmann_json")
find_package(nlohmann_json REQUIRED NO_DEFAULT_PATH)

include_directories(${CMAKE_SOURCE_DIR}/include)
add_subdirectory(src)
add_subdirectory(examples)
