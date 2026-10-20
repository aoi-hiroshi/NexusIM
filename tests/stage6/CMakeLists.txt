cmake_minimum_required(VERSION 3.10)
project(nexusim_stage6_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

add_executable(stage6_protocol_tests protocol_tests.cpp)
target_include_directories(stage6_protocol_tests PRIVATE
    ../../chatserver/include
    ../../chatserver/thirdparty)
if(MSVC)
    target_compile_options(stage6_protocol_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(stage6_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME stage6_protocol_tests COMMAND stage6_protocol_tests)
