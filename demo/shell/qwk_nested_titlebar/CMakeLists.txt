add_executable(shell_demo_qwk_nested_titlebar WIN32 main.cpp)
set_target_properties(shell_demo_qwk_nested_titlebar PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
target_compile_options(shell_demo_qwk_nested_titlebar PRIVATE /utf-8)
target_compile_definitions(shell_demo_qwk_nested_titlebar PRIVATE
    QWK_CORE_STATIC
    QWK_WIDGETS_STATIC)
target_link_libraries(shell_demo_qwk_nested_titlebar PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    QWindowKit::Widgets)
if(WIN32)
    target_link_libraries(shell_demo_qwk_nested_titlebar PRIVATE
        uxtheme dwmapi)
endif()
