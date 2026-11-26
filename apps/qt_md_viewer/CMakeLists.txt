set(TARGET_NAME "qt_md_viewer")

# 获取Qt的路径,后续用于查找Qt安装路径下的一些工具
get_filename_component(_md_viewer_qt_prefix "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
# ===============================
# 翻译管线：lupdate 收集 tr() 串 -> ts；lrelease 编译 -> qm；
# qm 与字体一样直接放到 exe 旁的 translations/ 目录，由 main.cpp 按语言加载。
# 界面源语言是英文（源码里 tr() 的原文），zh_CN.ts 是中文翻译。
# 加了新的 tr() 串后手动跑一次：cmake --build <build> --target update_translations
find_program(QLITEMD_LUPDATE  NAMES lupdate HINTS "${_md_viewer_qt_prefix}/bin")
find_program(QLITEMD_LRELEASE NAMES lrelease HINTS "${_md_viewer_qt_prefix}/bin")

set(TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/qt_md_viewer_zh_CN.ts
)
set(_i18n_target_name ${TARGET_NAME}_i18n)

# 缺少 Qt 语言工具时跳过翻译，界面按英文源文运行
if(QLITEMD_LUPDATE AND QLITEMD_LRELEASE)
    add_custom_target(update_translations
        COMMAND ${QLITEMD_LUPDATE} -recursive ${CMAKE_CURRENT_SOURCE_DIR} -ts ${TS_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating translations from source code (.ts)..."
    )

    # lrelease 的 -qm 只接受文件路径，逐个 ts 显式指定 qm；
    # 编译完把 qm 拷到 exe 旁的 translations/
    set(_i18n_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/translations
        COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${TARGET_NAME}>/translations)
    set(QM_FILES "")
    foreach(TS_FILE ${TS_FILES})
        get_filename_component(TS_NAME ${TS_FILE} NAME_WE)
        set(_qm ${CMAKE_CURRENT_BINARY_DIR}/translations/${TS_NAME}.qm)
        list(APPEND QM_FILES ${_qm})
        list(APPEND _i18n_commands
            COMMAND ${QLITEMD_LRELEASE} ${TS_FILE} -qm ${_qm}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_qm}
                    $<TARGET_FILE_DIR:${TARGET_NAME}>/translations/)
    endforeach()
    add_custom_target(${_i18n_target_name} ${_i18n_commands}
        COMMENT "Compiling translations (.qm)...")
else()
    message(STATUS "lupdate/lrelease not found; qt_md_viewer runs untranslated")
endif()

set(ALL_SRCS
    main.cpp
    activity_bar.cpp
    activity_bar.h
    bookmarks.cpp
    bookmarks.h
    bookmarks_page.cpp
    bookmarks_page.h
    reading_positions.cpp
    reading_positions.h
    document_index.cpp
    document_index.h
    document_tab.cpp
    document_tab.h
    document_tab_bar.cpp
    document_tab_bar.h
    documents_page.cpp
    documents_page.h
    side_panel.cpp
    side_panel.h
    side_panel_delegates.cpp
    side_panel_delegates.h
    side_panel_roles.h
    side_view.cpp
    side_view.h
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
    search_controller.cpp
    search_controller.h
    search_page.cpp
    search_page.h
    session_controller.cpp
    session_controller.h
    settings_panel.cpp
    settings_panel.h
    text_search.cpp
    text_search.h
    render_status_panel.cpp
    render_status_panel.h
    theme_icons.h
    top_tool_bar.cpp
    top_tool_bar.h
    welcome_view.cpp
    welcome_view.h
    qt_md_viewer.qrc
)

add_executable(${TARGET_NAME} WIN32 ${ALL_SRCS})

# 每次构建都重新编译 qm（ts 由 update_translations 手动维护，避免
# lupdate 每次构建都改写源目录里的 ts）
if(TARGET ${_i18n_target_name})
    add_dependencies(${TARGET_NAME} ${_i18n_target_name})
endif()


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
    # 翻译产物与字体同级：exe 旁 translations/ 目录
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations/"
        DESTINATION bin/translations COMPONENT Runtime)

    if(WIN32)
        # FluentUI3Style/ExWidgets are built in-tree and are not Qt plugins.
        install(TARGETS FluentUI3Style ExWidgets
            RUNTIME DESTINATION bin COMPONENT Runtime
            LIBRARY DESTINATION lib COMPONENT Runtime)
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

