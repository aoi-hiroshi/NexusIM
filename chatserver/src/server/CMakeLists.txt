# 定义了一个SRC_LIST变量，包含了该目录下所有的源文件
aux_source_directory(. SRC_LIST)
aux_source_directory(./db DB_LIST)
aux_source_directory(./model MODEL_LIST)
aux_source_directory(./redis REDIS_LIST)
aux_source_directory(./concurrency CONCURRENCY_LIST)
aux_source_directory(./security SECURITY_LIST)
aux_source_directory(./rpc RPC_LIST)

find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient)
find_library(HIREDIS_LIBRARY NAMES hiredis)
find_library(CHAT_MUDUO_NET_LIBRARY NAMES muduo_net)
find_library(CHAT_MUDUO_BASE_LIBRARY NAMES muduo_base)
if(NOT MYSQLCLIENT_LIBRARY OR NOT HIREDIS_LIBRARY OR
   NOT CHAT_MUDUO_NET_LIBRARY OR NOT CHAT_MUDUO_BASE_LIBRARY)
    message(FATAL_ERROR "ChatServer requires mysqlclient, hiredis and Muduo libraries")
endif()

# 指定生成可执行文件
add_executable(ChatServer ${SRC_LIST} ${DB_LIST} ${MODEL_LIST} ${REDIS_LIST}
               ${CONCURRENCY_LIST} ${SECURITY_LIST} ${RPC_LIST})
# 指定可执行文件链接时需要依赖的库文件
target_link_libraries(ChatServer
    ${CHAT_MUDUO_NET_LIBRARY}
    ${CHAT_MUDUO_BASE_LIBRARY}
    ${MYSQLCLIENT_LIBRARY}
    ${HIREDIS_LIBRARY}
    Threads::Threads
    OpenSSL::Crypto
    mprpc
    nexusim_user_proto
    nexusim_ai_proto)

# Allows a Linux staging directory to receive the single configurable binary;
# node identity and listen port are supplied by each instance's environment.
install(TARGETS ChatServer RUNTIME DESTINATION bin)
