find_package(CURL REQUIRED)

add_executable(AIService
    main.cpp
    ai_service_impl.cpp
    ai_types.cpp
    conversation_manager.cpp
    deepseek_client.cpp
    deepseek_response.cpp)

target_include_directories(AIService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/chatserver/include)

target_link_libraries(AIService PRIVATE
    mprpc
    nexusim_ai_proto
    Threads::Threads)

if(TARGET CURL::libcurl)
    target_link_libraries(AIService PRIVATE CURL::libcurl)
else()
    target_include_directories(AIService PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(AIService PRIVATE ${CURL_LIBRARIES})
endif()

set_target_properties(AIService PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

install(TARGETS AIService RUNTIME DESTINATION bin)
