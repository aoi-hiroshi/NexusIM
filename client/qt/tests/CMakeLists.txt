function(add_nexusim_qt_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE nexusim_qt_core Qt5::Test Qt5::Network)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 15)
endfunction()

add_nexusim_qt_test(qt_framecodec_tests test_framecodec.cpp)
add_nexusim_qt_test(qt_protocol_tests test_protocol.cpp)
add_nexusim_qt_test(qt_requestmanager_tests test_requestmanager.cpp)
add_nexusim_qt_test(qt_networkclient_tests test_networkclient.cpp)
add_nexusim_qt_test(qt_aicontroller_tests test_aicontroller.cpp)

add_executable(qt_aichatwidget_tests
    test_aichatwidget.cpp
    ../ui/aichatwidget.cpp)
target_link_libraries(qt_aichatwidget_tests PRIVATE
    nexusim_qt_core Qt5::Test Qt5::Network Qt5::Widgets)
add_test(NAME qt_aichatwidget_tests COMMAND qt_aichatwidget_tests)
set_tests_properties(qt_aichatwidget_tests PROPERTIES TIMEOUT 15)
