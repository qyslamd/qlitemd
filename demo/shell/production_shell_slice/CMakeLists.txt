add_executable(shell_demo_production WIN32
    main.cpp
    "${PROJECT_SOURCE_DIR}/apps/qt_md_viewer/document_tab_bar.cpp")
set_target_properties(shell_demo_production PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
target_include_directories(shell_demo_production PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/qt_md_viewer")
target_compile_options(shell_demo_production PRIVATE /utf-8)
target_compile_definitions(shell_demo_production PRIVATE
    QWK_CORE_STATIC
    QWK_WIDGETS_STATIC)
target_link_libraries(shell_demo_production PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Frameless
    FluentUI3Style)
if(WIN32)
    target_link_libraries(shell_demo_production PRIVATE
        uxtheme dwmapi)
endif()
