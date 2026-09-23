cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

# Shared pin-assignment state (contract owned by the integrator, not a subagent).
add_library(studio_config STATIC pin_assignment_model.cpp pin_assignment_model.h)
set_target_properties(studio_config PROPERTIES AUTOMOC ON)
target_compile_features(studio_config PUBLIC cxx_std_17)
target_include_directories(studio_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_config PUBLIC studio_board Qt6::Core)

enable_testing()
add_executable(pin_assignment_model_test tests/pin_assignment_model_test.cpp)
set_target_properties(pin_assignment_model_test PROPERTIES AUTOMOC ON)
target_link_libraries(pin_assignment_model_test PRIVATE studio_config Qt6::Test)
target_compile_definitions(pin_assignment_model_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME pin_assignment_model COMMAND pin_assignment_model_test)
set_tests_properties(pin_assignment_model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
