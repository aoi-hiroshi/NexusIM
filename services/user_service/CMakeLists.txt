find_library(USER_MYSQLCLIENT_LIBRARY NAMES mysqlclient)
find_library(USER_MUDUO_BASE_LIBRARY NAMES muduo_base)
if(NOT USER_MYSQLCLIENT_LIBRARY OR NOT USER_MUDUO_BASE_LIBRARY)
    message(FATAL_ERROR "UserService requires mysqlclient and muduo_base")
endif()

add_executable(UserService
    main.cpp
    userserviceimpl.cpp
    ${CMAKE_SOURCE_DIR}/chatserver/src/server/db/db.cpp
    ${CMAKE_SOURCE_DIR}/chatserver/src/server/model/usermodel.cpp
    ${CMAKE_SOURCE_DIR}/chatserver/src/server/security/passwordhasher.cpp)

target_include_directories(UserService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/chatserver/include
    ${CMAKE_SOURCE_DIR}/chatserver/include/server
    ${CMAKE_SOURCE_DIR}/chatserver/include/server/db
    ${CMAKE_SOURCE_DIR}/chatserver/include/server/model
    ${CMAKE_SOURCE_DIR}/chatserver/include/server/security)

target_link_libraries(UserService PRIVATE
    mprpc
    nexusim_user_proto
    ${USER_MYSQLCLIENT_LIBRARY}
    ${USER_MUDUO_BASE_LIBRARY}
    OpenSSL::Crypto
    Threads::Threads)

set_target_properties(UserService PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

install(TARGETS UserService RUNTIME DESTINATION bin)
