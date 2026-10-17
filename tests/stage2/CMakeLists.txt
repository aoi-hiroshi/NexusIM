cmake_minimum_required(VERSION 3.10)
project(nexusim_stage2_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stage2_cluster_protocol_tests cluster_protocol_tests.cpp)
target_include_directories(stage2_cluster_protocol_tests PRIVATE
    ../../chatserver/include
    ../../chatserver/thirdparty)

if(MSVC)
    target_compile_options(stage2_cluster_protocol_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(stage2_cluster_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME stage2_cluster_protocol_tests COMMAND stage2_cluster_protocol_tests)
