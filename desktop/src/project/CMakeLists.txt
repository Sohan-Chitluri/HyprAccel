cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)

# Persists desktop configuration into the same project store the web editor
# (mbd/editor/server.js) uses. Only needs ClockConfig from ../clock, a
# header-only struct; it does not link studio_clock.
#
# Also owns the MBD graph persistence/codegen bridge (Agent C,
# desktop/docs/mbd_graph_contract.md §7): ordered_json.cpp implements
# Hypr::Json::parse()/stringify() declared in ../mbd/contract/ordered_json.h;
# GraphStore/GraphCodegenRunner/CodegenOutputView build on it.
add_library(studio_project STATIC
    project_store.cpp project_store.h
    ordered_json.cpp
    graph_store.cpp graph_store.h
    graph_codegen_runner.cpp graph_codegen_runner.h
    codegen_output_view.cpp codegen_output_view.h)
set_target_properties(studio_project PROPERTIES AUTOMOC ON)
target_compile_features(studio_project PUBLIC cxx_std_17)
target_include_directories(studio_project PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../clock
    ${CMAKE_CURRENT_SOURCE_DIR}/../mbd/contract)
target_link_libraries(studio_project PUBLIC studio_config Qt6::Core Qt6::Widgets)
target_compile_definitions(studio_project PUBLIC
    HYPRACCEL_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    GRAPH_TO_C_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/codegen/graph_to_c.js")

enable_testing()

add_executable(project_store_test tests/project_store_test.cpp)
set_target_properties(project_store_test PROPERTIES AUTOMOC ON)
target_link_libraries(project_store_test PRIVATE studio_project Qt6::Test)
target_compile_definitions(project_store_test PRIVATE
    BOARD_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/boards.yaml")
add_test(NAME project_store COMMAND project_store_test)
set_tests_properties(project_store PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)

add_executable(ordered_json_test tests/ordered_json_test.cpp)
set_target_properties(ordered_json_test PROPERTIES AUTOMOC ON)
target_link_libraries(ordered_json_test PRIVATE studio_project Qt6::Test)
add_test(NAME ordered_json COMMAND ordered_json_test)
set_tests_properties(ordered_json PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_executable(graph_store_test tests/graph_store_test.cpp)
set_target_properties(graph_store_test PROPERTIES AUTOMOC ON)
target_link_libraries(graph_store_test PRIVATE studio_project Qt6::Test Qt6::Network)
target_compile_definitions(graph_store_test PRIVATE
    REPO_ROOT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    DEMO_PROJECT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../.hypraccel/projects/demo_project"
    GRAPH_EDITOR_HTML_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/editor/src/graph_editor.html"
    MBD_SERVER_JS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/editor/server.js"
    MBD_EDITOR_DIR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/editor"
    SCHEMA_EXAMPLES_DIR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/schema/examples"
    TEST_GRAPHS_DIR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/test_graphs")
add_test(NAME graph_store COMMAND graph_store_test)
set_tests_properties(graph_store PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

add_executable(graph_codegen_runner_test tests/graph_codegen_runner_test.cpp)
set_target_properties(graph_codegen_runner_test PROPERTIES AUTOMOC ON)
target_link_libraries(graph_codegen_runner_test PRIVATE studio_project Qt6::Test Qt6::Network)
target_compile_definitions(graph_codegen_runner_test PRIVATE
    GRAPH_TO_C_SCRIPT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/codegen/graph_to_c.js"
    MBD_EDITOR_DIR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/editor"
    SCHEMA_EXAMPLES_DIR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/schema/examples"
    TEST_GRAPHS_DIR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../mbd/test_graphs"
    DEMO_PROJECT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../../.hypraccel/projects/demo_project")
add_test(NAME graph_codegen_runner COMMAND graph_codegen_runner_test)
set_tests_properties(graph_codegen_runner PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
