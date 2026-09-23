cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

# Right-click pin function menu + Pin Inspector text.
add_library(studio_pinmenu STATIC pin_context_menu.cpp pin_context_menu.h)
set_target_properties(studio_pinmenu PROPERTIES AUTOMOC ON)
target_compile_features(studio_pinmenu PUBLIC cxx_std_17)
target_include_directories(studio_pinmenu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_pinmenu PUBLIC studio_config Qt6::Widgets)

enable_testing()
add_executable(pin_context_menu_test tests/pin_context_menu_test.cpp)
set_target_properties(pin_context_menu_test PROPERTIES AUTOMOC ON)
target_link_libraries(pin_context_menu_test PRIVATE studio_pinmenu Qt6::Test)
target_compile_definitions(pin_context_menu_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME pin_context_menu COMMAND pin_context_menu_test)
set_tests_properties(pin_context_menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
