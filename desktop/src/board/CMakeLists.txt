cmake_minimum_required(VERSION 3.20)

# This component can also be built alone, without the desktop application.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(HyprBoardCatalog LANGUAGES CXX)
    enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Test)
find_package(yaml-cpp REQUIRED)

add_library(studio_board STATIC board_catalog.cpp board_catalog.h)
target_compile_features(studio_board PUBLIC cxx_std_17)
target_include_directories(studio_board PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_board PUBLIC Qt6::Core PRIVATE yaml-cpp::yaml-cpp)
# The public data model uses the field name "signals"; use Qt's explicit macros.
target_compile_definitions(studio_board PUBLIC QT_NO_KEYWORDS)

add_executable(board_catalog_test tests/board_catalog_test.cpp)
set_target_properties(board_catalog_test PROPERTIES AUTOMOC ON)
target_link_libraries(board_catalog_test PRIVATE studio_board Qt6::Test)
target_compile_definitions(board_catalog_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME board_catalog COMMAND board_catalog_test)
set_tests_properties(board_catalog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
