set(VIEWER_DIR ${PROJECT_SOURCE_DIR}/apps/qt_md_viewer)

add_unit_test(
    NAME test_viewer_navigation
    SOURCES navigation_history_test.cpp
            ${VIEWER_DIR}/navigation_history.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_tab_session_codec
    SOURCES tab_session_codec_test.cpp
            ${VIEWER_DIR}/tab_session_codec.cpp
            ${VIEWER_DIR}/tab_session_codec.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_md_viewer_settings
    SOURCES md_viewer_settings_test.cpp
            ${VIEWER_DIR}/md_viewer_settings.cpp
            ${VIEWER_DIR}/md_viewer_settings.h
    INCLUDES ${VIEWER_DIR}
             ${PROJECT_SOURCE_DIR}/src/markdown_view
    QT_MODULES Core Gui
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_theme_icons
    SOURCES theme_icons_test.cpp
            ${VIEWER_DIR}/qt_md_viewer.qrc
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets
    LABELS MARKDOWN
    NO_GTEST_MAIN
)
# 图标测试的真实对象是 qrc 路径本身；打开 AUTORCC 才会把两套 svg 编进测试
set_target_properties(test_theme_icons PROPERTIES AUTORCC ON)

add_unit_test(
    NAME test_document_index
    SOURCES document_index_test.cpp
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)

if(TARGET Frameless)
    add_unit_test(
        NAME test_titlebar_hooks
        SOURCES titlebar_hooks_test.cpp
        INCLUDES ${PROJECT_SOURCE_DIR}/3rdparty/fluentui/frameless
        LIBS Frameless
        QT_MODULES Widgets Test
        LABELS MARKDOWN
        NO_GTEST_MAIN
    )
endif()

add_unit_test(
    NAME test_side_panel
    SOURCES side_panel_test.cpp
            ${VIEWER_DIR}/side_panel.cpp
            ${VIEWER_DIR}/side_panel.h
            ${VIEWER_DIR}/document_index.cpp
            ${VIEWER_DIR}/bookmarks.cpp
    INCLUDES ${VIEWER_DIR}
             ${PROJECT_SOURCE_DIR}/3rdparty/fluentui/fluentui3style
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_bookmarks
    SOURCES bookmarks_test.cpp
            ${VIEWER_DIR}/bookmarks.cpp
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_reading_positions
    SOURCES reading_positions_test.cpp
            ${VIEWER_DIR}/reading_positions.cpp
            ${VIEWER_DIR}/reading_positions.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_text_search
    SOURCES text_search_test.cpp
            ${VIEWER_DIR}/text_search.cpp
            ${VIEWER_DIR}/text_search.h
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)
set_tests_properties(test_text_search PROPERTIES TIMEOUT 120)

add_unit_test(
    NAME test_quick_open
    SOURCES quick_open_test.cpp
            ${VIEWER_DIR}/quick_open.cpp
            ${VIEWER_DIR}/quick_open.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_reader_appearance
    SOURCES reader_appearance_test.cpp
            ${VIEWER_DIR}/reader_preferences.cpp
            ${VIEWER_DIR}/reader_preferences.h
            ${VIEWER_DIR}/reader_appearance_popup.cpp
            ${VIEWER_DIR}/reader_appearance_popup.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_welcome_view
    SOURCES welcome_view_test.cpp
            ${VIEWER_DIR}/welcome_view.cpp
            ${VIEWER_DIR}/welcome_view.h
            ${VIEWER_DIR}/document_index.cpp
            ${VIEWER_DIR}/document_index.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_reading_progress
    SOURCES reading_progress_test.cpp
            ${VIEWER_DIR}/reading_progress.cpp
            ${VIEWER_DIR}/reading_progress.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_document_bar
    SOURCES document_bar_test.cpp
            ${VIEWER_DIR}/document_bar.cpp
            ${VIEWER_DIR}/document_bar.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_top_tool_bar
    SOURCES top_tool_bar_test.cpp
            ${VIEWER_DIR}/top_tool_bar.cpp
            ${VIEWER_DIR}/top_tool_bar.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_activity_bar
    SOURCES activity_bar_test.cpp
            ${VIEWER_DIR}/activity_bar.cpp
            ${VIEWER_DIR}/activity_bar.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_welcome_bar
    SOURCES welcome_bar_test.cpp
            ${VIEWER_DIR}/welcome_bar.cpp
            ${VIEWER_DIR}/welcome_bar.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_document_tab_bar
    SOURCES document_tab_bar_test.cpp
            ${VIEWER_DIR}/document_tab_bar.cpp
            ${VIEWER_DIR}/document_tab_bar.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_reader_page
    SOURCES reader_page_test.cpp
            ${VIEWER_DIR}/reader_page.cpp
            ${VIEWER_DIR}/reader_page.h
            ${VIEWER_DIR}/reading_progress.cpp
            ${VIEWER_DIR}/reading_progress.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

add_unit_test(
    NAME test_image_lightbox
    SOURCES image_lightbox_test.cpp
            ${VIEWER_DIR}/image_lightbox.cpp
            ${VIEWER_DIR}/image_lightbox.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

# 设置对话框：左侧分类列表 + 右侧分页，控件按 objectName 可达
add_unit_test(
    NAME test_settings_panel_layout
    SOURCES settings_panel_test.cpp
            ${VIEWER_DIR}/settings_panel.cpp
            ${VIEWER_DIR}/settings_panel.h
            ${VIEWER_DIR}/settings_dialog.cpp
            ${VIEWER_DIR}/settings_dialog.h
    INCLUDES ${VIEWER_DIR}
    LIBS qlitemd::markdown_view
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

# 渲染状态面板：验证 outlineChanged -> RefreshStatus 这条刷新链路。
# 面板用了 ExWidgets 的进度环与折叠容器，Fluent 路线关闭时不编这个测试
if(TARGET ExWidgets::ExWidgets)
    add_unit_test(
        NAME test_render_status_panel
        SOURCES status_panel_test.cpp
                ${VIEWER_DIR}/render_status_panel.cpp
        INCLUDES ${VIEWER_DIR}
                 ${PROJECT_SOURCE_DIR}/tests/support
        LIBS qlitemd::markdown_view ExWidgets::ExWidgets
        QT_MODULES Widgets Test
        LABELS MARKDOWN
        NO_GTEST_MAIN
    )
    set_tests_properties(test_render_status_panel PROPERTIES TIMEOUT 120)
    if(WIN32)
        target_link_libraries(test_render_status_panel PRIVATE psapi)
    endif()
endif()
