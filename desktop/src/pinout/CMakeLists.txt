cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

add_library(studio_pinout STATIC pinout_scene.cpp pinout_scene.h)
set_target_properties(studio_pinout PROPERTIES AUTOMOC ON)
target_compile_features(studio_pinout PUBLIC cxx_std_17)
target_include_directories(studio_pinout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_pinout PUBLIC studio_config studio_theme Qt6::Widgets)

enable_testing()
add_executable(pinout_scene_test tests/pinout_scene_test.cpp)
set_target_properties(pinout_scene_test PROPERTIES AUTOMOC ON)
target_link_libraries(pinout_scene_test PRIVATE studio_pinout Qt6::Test)
target_compile_definitions(pinout_scene_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME pinout_scene COMMAND pinout_scene_test)
set_tests_properties(pinout_scene PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
