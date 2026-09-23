cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(StudioTheme LANGUAGES CXX)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
endif()

add_library(studio_theme STATIC theme.cpp theme.h)
target_compile_features(studio_theme PUBLIC cxx_std_17)
target_include_directories(studio_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_theme PUBLIC Qt6::Widgets)

# Enable in this directory even when the parent enables testing after add_subdirectory.
enable_testing()
add_executable(studio_theme_tests tests/theme_test.cpp)
set_target_properties(studio_theme_tests PROPERTIES AUTOMOC ON)
target_link_libraries(studio_theme_tests PRIVATE studio_theme Qt6::Test)
add_test(NAME studio_theme COMMAND studio_theme_tests)
set_tests_properties(studio_theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
