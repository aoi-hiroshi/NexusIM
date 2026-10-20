find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME stage7_source_guards
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/source_guards.py)
