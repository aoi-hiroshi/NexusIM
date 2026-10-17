cmake_minimum_required(VERSION 3.10)
project(nexusim_stage1_tests LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stage1_core_tests
    core_tests.cpp
    ../../chatserver/src/server/concurrency/boundedexecutor.cpp
    ../../chatserver/src/server/concurrency/connectionstrand.cpp
    ../../chatserver/src/server/concurrency/pendingrequest.cpp)

target_include_directories(stage1_core_tests PRIVATE ../../chatserver/include)

target_link_libraries(stage1_core_tests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(stage1_core_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(stage1_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME stage1_core_tests COMMAND stage1_core_tests)

option(NEXUSIM_BUILD_SECURITY_TESTS "Build OpenSSL password/session tests" OFF)
if(NEXUSIM_BUILD_SECURITY_TESTS)
    find_package(OpenSSL REQUIRED)
    add_executable(stage1_security_tests
        security_tests.cpp
        ../../chatserver/src/server/security/passwordhasher.cpp)
    target_include_directories(stage1_security_tests PRIVATE ../../chatserver/include)
    target_link_libraries(stage1_security_tests PRIVATE OpenSSL::Crypto)
    add_test(NAME stage1_security_tests COMMAND stage1_security_tests)
endif()

