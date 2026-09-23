cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

# Persists desktop configuration into the same project store the web editor
# (mbd/editor/server.js) uses. Only needs ClockConfig from ../clock, a
# header-only struct; it does not link studio_clock.
add_library(studio_project STATIC project_store.cpp project_store.h)
set_target_properties(studio_project PROPERTIES AUTOMOC ON)
target_compile_features(studio_project PUBLIC cxx_std_17)
target_include_directories(studio_project PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../clock)
target_link_libraries(studio_project PUBLIC studio_config Qt6::Core)
target_compile_definitions(studio_project PUBLIC
    HYPRACCEL_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../..")

enable_testing()
add_executable(project_store_test tests/project_store_test.cpp)
set_target_properties(project_store_test PROPERTIES AUTOMOC ON)
target_link_libraries(project_store_test PRIVATE studio_project Qt6::Test)
target_compile_definitions(project_store_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME project_store COMMAND project_store_test)
set_tests_properties(project_store PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
