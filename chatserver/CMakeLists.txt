cmake_minimum_required(VERSION 3.10)
project(NexusIMChat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

if(NOT TARGET nexusim_user_proto)
    find_package(Protobuf REQUIRED)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../proto
                     ${CMAKE_BINARY_DIR}/nexusim-proto)
endif()
if(NOT TARGET mprpc)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mprpc
                     ${CMAKE_BINARY_DIR}/nexusim-mprpc)
endif()

# 配置编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

# 配置最终的可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 配置头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/server)
include_directories(${PROJECT_SOURCE_DIR}/include/server/db)
include_directories(${PROJECT_SOURCE_DIR}/include/server/model)
include_directories(${PROJECT_SOURCE_DIR}/include/server/redis)
include_directories(${PROJECT_SOURCE_DIR}/include/server/concurrency)
include_directories(${PROJECT_SOURCE_DIR}/include/server/security)
include_directories(${PROJECT_SOURCE_DIR}/include/server/rpc)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty)

# 加载子目录
add_subdirectory(src)
