cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

# Live pin-conflict checking for the Pin Inspector: shells out to
# boards/codegen/check_pin_conflicts.js (async QProcess), reusing
# ProjectStore::hardwareJson() for the request payload. No conflict-rule
# logic lives here — see pin_check_runner.h.
add_library(studio_pincheck STATIC pin_check_runner.cpp pin_check_runner.h)
set_target_properties(studio_pincheck PROPERTIES AUTOMOC ON)
target_compile_features(studio_pincheck PUBLIC cxx_std_17)
target_include_directories(studio_pincheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_pincheck PUBLIC studio_project studio_board Qt6::Core)

enable_testing()
add_executable(pin_check_runner_test tests/pin_check_runner_test.cpp)
set_target_properties(pin_check_runner_test PROPERTIES AUTOMOC ON)
target_link_libraries(pin_check_runner_test PRIVATE studio_pincheck Qt6::Test)
target_compile_definitions(pin_check_runner_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml"
    PIN_CHECK_SCRIPT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/codegen/check_pin_conflicts.js")
add_test(NAME pin_check_runner COMMAND pin_check_runner_test)
set_tests_properties(pin_check_runner PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
