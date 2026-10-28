set(VIEWER_DIR ${PROJECT_SOURCE_DIR}/examples/md_viewer)

add_unit_test(
    NAME test_viewer_navigation
    SOURCES navigation_history_test.cpp
            ${VIEWER_DIR}/navigation_history.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)

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
    NAME test_documents_panel
    SOURCES documents_panel_test.cpp
            ${VIEWER_DIR}/documents_panel.cpp
            ${VIEWER_DIR}/documents_panel.h
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
            ${VIEWER_DIR}/reader_appearance.cpp
            ${VIEWER_DIR}/reader_appearance.h
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
    NAME test_image_lightbox
    SOURCES image_lightbox_test.cpp
            ${VIEWER_DIR}/image_lightbox.cpp
            ${VIEWER_DIR}/image_lightbox.h
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)

# 示例里的渲染状态面板：验证 outlineChanged -> RefreshStatus 这条刷新链路。
# 面板用了 ExWidgets 的进度环与折叠容器，Fluent 路线关闭时不编这个测试
if(TARGET ExWidgets::ExWidgets)
    add_unit_test(
        NAME test_render_settings_panel
        SOURCES status_panel_test.cpp
                ${VIEWER_DIR}/render_status_panel.cpp
        INCLUDES ${VIEWER_DIR}
                 ${PROJECT_SOURCE_DIR}/tests/support
        LIBS qlitemd::markdown_view ExWidgets::ExWidgets
        QT_MODULES Widgets Test
        LABELS MARKDOWN
        NO_GTEST_MAIN
    )
    set_tests_properties(test_render_settings_panel PROPERTIES TIMEOUT 120)
    if(WIN32)
        target_link_libraries(test_render_settings_panel PRIVATE psapi)
    endif()
endif()
