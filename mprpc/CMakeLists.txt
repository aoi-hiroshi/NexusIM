cmake_minimum_required(VERSION 3.10)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NexusIMMprpc LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    find_package(Protobuf REQUIRED)
    find_package(Threads REQUIRED)
endif()

find_path(MUDUO_INCLUDE_DIR muduo/net/TcpServer.h)
find_library(MUDUO_NET_LIBRARY NAMES muduo_net)
find_library(MUDUO_BASE_LIBRARY NAMES muduo_base)
find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h)
find_library(ZOOKEEPER_LIBRARY NAMES zookeeper_mt)
if(NOT MUDUO_INCLUDE_DIR OR NOT MUDUO_NET_LIBRARY OR NOT MUDUO_BASE_LIBRARY)
    message(FATAL_ERROR "Muduo headers and muduo_net/muduo_base libraries are required")
endif()
if(NOT ZOOKEEPER_INCLUDE_DIR OR NOT ZOOKEEPER_LIBRARY)
    message(FATAL_ERROR "ZooKeeper C client headers and zookeeper_mt are required")
endif()

protobuf_generate_cpp(MPRPC_ENVELOPE_SRCS MPRPC_ENVELOPE_HDRS
                      ${CMAKE_CURRENT_SOURCE_DIR}/proto/rpc_envelope.proto)

add_library(mprpc
    src/boundedexecutor.cc
    src/logger.cc
    src/mprpcapplication.cc
    src/mprpcchannel.cc
    src/mprpcconfig.cc
    src/mprpccontroller.cc
    src/rpcprovider.cc
    src/zookeeperutil.cc
    ${MPRPC_ENVELOPE_SRCS}
    ${MPRPC_ENVELOPE_HDRS})

# The ZooKeeper C headers expose the synchronous API (zoo_create,
# zoo_exists, and related calls) only for the multi-threaded client build.
# Keep this definition on the mprpc target so it matches zookeeper_mt.
target_compile_definitions(mprpc PUBLIC THREADED)

target_include_directories(mprpc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
        ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC
        ${MUDUO_INCLUDE_DIR}
        ${ZOOKEEPER_INCLUDE_DIR})

if(TARGET protobuf::libprotobuf)
    target_link_libraries(mprpc PUBLIC protobuf::libprotobuf)
else()
    target_include_directories(mprpc PUBLIC ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(mprpc PUBLIC ${Protobuf_LIBRARIES})
endif()
target_link_libraries(mprpc PUBLIC
    ${MUDUO_NET_LIBRARY}
    ${MUDUO_BASE_LIBRARY}
    ${ZOOKEEPER_LIBRARY}
    Threads::Threads)

option(NEXUSIM_BUILD_MPRPC_COURSE_EXAMPLES
       "Build the unmodified course examples" OFF)
if(NEXUSIM_BUILD_MPRPC_COURSE_EXAMPLES)
    add_subdirectory(example)
endif()
