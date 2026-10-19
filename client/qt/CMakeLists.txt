cmake_minimum_required(VERSION 3.10)
project(NexusIMQtClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(NEXUSIM_QT_BUILD_TESTS "Build NexusIM Qt client tests" ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Network Widgets)

add_library(nexusim_qt_core STATIC
    protocol/framecodec.cpp
    protocol/messageprotocol.cpp
    network/networkclient.cpp
    network/requestmanager.cpp
    models/conversationmodel.cpp
    controllers/sessioncontroller.cpp
    controllers/authcontroller.cpp
    controllers/chatcontroller.cpp)

target_include_directories(nexusim_qt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nexusim_qt_core PUBLIC Qt5::Core Qt5::Network)
if(MSVC)
    target_compile_options(nexusim_qt_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(nexusim_qt_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(NexusIMClient WIN32
    app/main.cpp
    app/applicationcontroller.cpp
    ui/loginwindow.cpp
    ui/registerdialog.cpp
    ui/settingsdialog.cpp
    ui/mainwindow.cpp
    ui/chatwidget.cpp
    ui/conversationlist.cpp
    resources/resources.qrc)

target_include_directories(NexusIMClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(NexusIMClient PRIVATE nexusim_qt_core Qt5::Widgets)
set_target_properties(NexusIMClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NEXUSIM_QT_BUILD_TESTS)
    find_package(Qt5 5.12 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
