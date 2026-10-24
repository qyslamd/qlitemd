add_unit_test(
    NAME test_mdhtml
    SOURCES md_html_test.cpp
    LIBS qlitemd_mdhtml md4c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/mdhtml
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_mdhtml_highlight
    SOURCES md_highlight_test.cpp
    LIBS qlitemd_mdhtml md4c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/mdhtml
    LABELS MARKDOWN
)
