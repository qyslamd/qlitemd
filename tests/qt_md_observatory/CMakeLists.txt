add_unit_test(
    NAME test_workspace_model
    SOURCES workspace_model_test.cpp
            ${PROJECT_SOURCE_DIR}/apps/qt_md_observatory/workspace_model.cpp
    LIBS
    QT_MODULES Core
    INCLUDES
        ${PROJECT_SOURCE_DIR}/apps/qt_md_observatory
    LABELS MARKDOWN
    NO_GTEST_MAIN
)
set_tests_properties(test_workspace_model PROPERTIES TIMEOUT 120)

add_unit_test(
    NAME test_instrument_math
    SOURCES instrument_math_test.cpp
            ${PROJECT_SOURCE_DIR}/apps/qt_md_observatory/instrument_math.cpp
    LIBS
    QT_MODULES Core Gui
    INCLUDES
        ${PROJECT_SOURCE_DIR}/apps/qt_md_observatory
        ${PROJECT_SOURCE_DIR}/src/markdown_view
    LABELS MARKDOWN
)
set_tests_properties(test_instrument_math PROPERTIES TIMEOUT 120)

add_unit_test(
    NAME test_command_palette
    SOURCES command_palette_test.cpp
            ${PROJECT_SOURCE_DIR}/apps/qt_md_observatory/command_palette.cpp
    LIBS
    QT_MODULES Widgets Test
    INCLUDES
        ${PROJECT_SOURCE_DIR}/apps/qt_md_observatory
    LABELS MARKDOWN
    NO_GTEST_MAIN
)
set_tests_properties(test_command_palette PROPERTIES TIMEOUT 120)
