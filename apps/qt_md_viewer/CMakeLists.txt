set(TARGET_NAME "qt_md_viewer")

set(ALL_SRCS
    main.cpp
    activity_bar.cpp
    activity_bar.h
    bookmarks.cpp
    bookmarks.h
    reading_positions.cpp
    reading_positions.h
    document_index.cpp
    document_index.h
    document_tab.h
    document_bar.cpp
    document_bar.h
    document_tab_bar.cpp
    document_tab_bar.h
    side_panel.cpp
    side_panel.h
    image_lightbox.cpp
    image_lightbox.h
    md_viewer_settings.cpp
    md_viewer_settings.h
    md_viewer_window.cpp
    md_viewer_window.h
    navigation_history.cpp
    navigation_history.h
    quick_open.cpp
    quick_open.h
    reader_preferences.cpp
    reader_preferences.h
    reader_appearance_popup.cpp
    reader_appearance_popup.h
    reader_page.cpp
    reader_page.h
    reading_progress.cpp
    reading_progress.h
    settings_panel.cpp
    settings_panel.h
    text_search.cpp
    text_search.h
    render_status_panel.cpp
    render_status_panel.h
    settings_dialog.cpp
    settings_dialog.h
    tab_session_codec.cpp
    tab_session_codec.h
    theme_icons.h
    top_tool_bar.cpp
    top_tool_bar.h
    welcome_bar.cpp
    welcome_bar.h
    welcome_view.cpp
    welcome_view.h
    qt_md_viewer.qrc
)

add_executable(${TARGET_NAME} WIN32 ${ALL_SRCS})

if(WIN32)
    # 可执行文件图标与安装包共用 resources/icon.ico
    enable_language(RC)
    target_sources(${TARGET_NAME} PRIVATE qt_md_viewer.rc)
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    qlitemd::markdown_view
    FluentUI3Style
    Frameless
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
if(TARGET qlitemd::ratex)
    set(_md_viewer_formula_label "formula:auto")
else()
    set(_md_viewer_formula_label "QuickJS")
endif()
if(TARGET qlitemd::merman)
    set(_md_viewer_mermaid_label "mermaid:auto")
else()
    set(_md_viewer_mermaid_label "QuickJS")
endif()
if(QLITEMD_SVG_BACKEND STREQUAL "resvg")
    set(_md_viewer_svg_label "resvg")
else()
    set(_md_viewer_svg_label "QtSvg")
endif()
target_compile_definitions(${TARGET_NAME} PRIVATE
    APP_UI_FLUENT
    UNICODE
    _UNICODE
    QLITEMD_BACKEND_LABEL="${_md_viewer_formula_label} / ${_md_viewer_mermaid_label} / ${_md_viewer_svg_label}")

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE psapi)
endif()

# 界面字体放在可执行文件旁的 fonts/：不编进 qrc，启动时按路径注册
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/fonts"
    COMMENT "Copying bundled fonts next to ${TARGET_NAME}")

set_target_properties(${TARGET_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(QLITEMD_INSTALL)
    install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION bin
        COMPONENT Runtime)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
        DESTINATION bin COMPONENT Runtime)

    if(WIN32)
        # FluentUI3Style/ExWidgets are built in-tree and are not Qt plugins.
        install(TARGETS FluentUI3Style ExWidgets
            RUNTIME DESTINATION bin COMPONENT Runtime
            LIBRARY DESTINATION lib COMPONENT Runtime)
        get_filename_component(_md_viewer_qt_prefix
            "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
        find_program(QLITEMD_WINDEPLOYQT
            NAMES windeployqt
            HINTS "${_md_viewer_qt_prefix}/bin")
        if(QLITEMD_WINDEPLOYQT)
            install(CODE
                "execute_process(COMMAND \"${QLITEMD_WINDEPLOYQT}\" --dir \"\${CMAKE_INSTALL_PREFIX}/bin\" \"\${CMAKE_INSTALL_PREFIX}/bin/qt_md_viewer.exe\")"
                COMPONENT Runtime)
        endif()
    endif()
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX- /EHsc /utf-8)
endif()

if(QLITEMD_IS_TOP_LEVEL)
    configure_run_script("run_${TARGET_NAME}")
endif()

