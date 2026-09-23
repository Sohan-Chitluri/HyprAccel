cmake_minimum_required(VERSION 3.20)

# This component can also be built alone, without the desktop application.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(HyprClock LANGUAGES CXX)
    enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
find_package(yaml-cpp REQUIRED)

add_library(studio_clock STATIC clock_tree.cpp clock_tree.h clock_view.cpp clock_view.h)
set_target_properties(studio_clock PROPERTIES AUTOMOC ON)
target_compile_features(studio_clock PUBLIC cxx_std_17)
target_include_directories(studio_clock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_clock PUBLIC Qt6::Widgets studio_theme PRIVATE yaml-cpp::yaml-cpp)

add_executable(clock_tree_test tests/clock_tree_test.cpp)
set_target_properties(clock_tree_test PROPERTIES AUTOMOC ON)
target_link_libraries(clock_tree_test PRIVATE studio_clock Qt6::Test)
target_compile_definitions(clock_tree_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME clock_tree COMMAND clock_tree_test)
set_tests_properties(clock_tree PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)

add_executable(clock_view_test tests/clock_view_test.cpp)
set_target_properties(clock_view_test PROPERTIES AUTOMOC ON)
target_link_libraries(clock_view_test PRIVATE studio_clock Qt6::Test)
target_compile_definitions(clock_view_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME clock_view COMMAND clock_view_test)
set_tests_properties(clock_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
