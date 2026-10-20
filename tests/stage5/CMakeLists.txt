cmake_minimum_required(VERSION 3.10)
project(nexusim_stage5_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stage5_ai_core_tests
    ai_core_tests.cpp
    ../../services/ai_service/ai_types.cpp
    ../../services/ai_service/conversation_manager.cpp
    ../../services/ai_service/deepseek_response.cpp)
target_include_directories(stage5_ai_core_tests PRIVATE
    ../../services/ai_service
    ../../chatserver/thirdparty)

if(MSVC)
    target_compile_options(stage5_ai_core_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(stage5_ai_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME stage5_ai_core_tests COMMAND stage5_ai_core_tests)

find_package(CURL QUIET)
if(CURL_FOUND)
    add_executable(stage5_deepseek_client_probe
        deepseek_client_probe.cpp
        ../../services/ai_service/ai_types.cpp
        ../../services/ai_service/deepseek_client.cpp
        ../../services/ai_service/deepseek_response.cpp)
    target_include_directories(stage5_deepseek_client_probe PRIVATE
        ../../services/ai_service
        ../../chatserver/thirdparty)
    if(TARGET CURL::libcurl)
        target_link_libraries(stage5_deepseek_client_probe PRIVATE CURL::libcurl)
    else()
        target_include_directories(stage5_deepseek_client_probe PRIVATE
            ${CURL_INCLUDE_DIRS})
        target_link_libraries(stage5_deepseek_client_probe PRIVATE
            ${CURL_LIBRARIES})
    endif()
endif()

if(TARGET mprpc AND TARGET nexusim_ai_proto)
    add_executable(stage5_ai_service_rpc_probe ai_service_rpc_probe.cpp)
    target_link_libraries(stage5_ai_service_rpc_probe PRIVATE
        mprpc nexusim_ai_proto)
endif()
