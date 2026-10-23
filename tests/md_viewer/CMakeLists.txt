set(VIEWER_DIR ${CMAKE_SOURCE_DIR}/examples/md_viewer)

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
        INCLUDES ${CMAKE_SOURCE_DIR}/3rdparty/fluentui/frameless
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
             ${CMAKE_SOURCE_DIR}/3rdparty/fluentui/fluentui3style
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
