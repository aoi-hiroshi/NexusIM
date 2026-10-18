cmake_minimum_required(VERSION 3.10)
project(nexusim_stage3_portable_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stage3_rpc_frame_tests rpc_frame_tests.cpp)
target_include_directories(stage3_rpc_frame_tests PRIVATE
    ../../mprpc/src/include
    ../../chatserver/include)

if(MSVC)
    target_compile_options(stage3_rpc_frame_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(stage3_rpc_frame_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME stage3_rpc_frame_tests COMMAND stage3_rpc_frame_tests)

if(TARGET mprpc AND TARGET nexusim_user_proto)
    add_executable(stage3_rpc_protocol_tests rpc_protocol_tests.cpp)
    target_include_directories(stage3_rpc_protocol_tests PRIVATE
        ../../mprpc/src/include
        ../../chatserver/include)
    target_link_libraries(stage3_rpc_protocol_tests PRIVATE
        mprpc nexusim_user_proto)
    add_test(NAME stage3_rpc_protocol_tests COMMAND stage3_rpc_protocol_tests)

    add_executable(stage3_user_service_rpc_probe user_service_rpc_probe.cpp)
    target_link_libraries(stage3_user_service_rpc_probe PRIVATE
        mprpc nexusim_user_proto)

    add_executable(stage3_rpc_transport_probe rpc_transport_probe.cpp)
    target_include_directories(stage3_rpc_transport_probe PRIVATE
        ../../mprpc/src/include)
    target_link_libraries(stage3_rpc_transport_probe PRIVATE
        mprpc nexusim_user_proto)
endif()
