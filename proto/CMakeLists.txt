set(USER_SERVICE_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/user_service.proto)
protobuf_generate_cpp(USER_SERVICE_PROTO_SRCS USER_SERVICE_PROTO_HDRS
                      ${USER_SERVICE_PROTO})

add_library(nexusim_user_proto ${USER_SERVICE_PROTO_SRCS}
                               ${USER_SERVICE_PROTO_HDRS})
target_include_directories(nexusim_user_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
if(TARGET protobuf::libprotobuf)
    target_link_libraries(nexusim_user_proto PUBLIC protobuf::libprotobuf)
else()
    target_include_directories(nexusim_user_proto PUBLIC ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(nexusim_user_proto PUBLIC ${Protobuf_LIBRARIES})
endif()
