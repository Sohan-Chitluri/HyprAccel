cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

# GraphModel (document + selection + undo) and GraphCanvas (QGraphicsView
# rendering), driven entirely by the shared contract/ headers and by
# NodeTypeProvider. No node-type-specific logic lives here; see
# desktop/docs/mbd_graph_contract.md §7.
add_library(studio_mbd STATIC
    graph_model.cpp
    graph_model.h
    graph_canvas.cpp
    graph_canvas.h
)
set_target_properties(studio_mbd PROPERTIES AUTOMOC ON)
target_compile_features(studio_mbd PUBLIC cxx_std_17)
target_include_directories(studio_mbd PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/contract
)
target_link_libraries(studio_mbd PUBLIC Qt6::Core Qt6::Widgets studio_theme)

enable_testing()

add_executable(graph_model_test tests/graph_model_test.cpp tests/fake_node_type_provider.h)
set_target_properties(graph_model_test PROPERTIES AUTOMOC ON)
target_link_libraries(graph_model_test PRIVATE studio_mbd Qt6::Test)
add_test(NAME graph_model COMMAND graph_model_test)
set_tests_properties(graph_model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(graph_canvas_test tests/graph_canvas_test.cpp tests/fake_node_type_provider.h)
set_target_properties(graph_canvas_test PROPERTIES AUTOMOC ON)
target_link_libraries(graph_canvas_test PRIVATE studio_mbd Qt6::Test)
add_test(NAME graph_canvas COMMAND graph_canvas_test)
set_tests_properties(graph_canvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

