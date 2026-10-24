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

add_unit_test(
    NAME test_mdhtml_svg
    SOURCES md_svg_test.cpp
    LIBS qlitemd_mdhtml md4c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/mdhtml
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_mdhtml_font_metrics
    SOURCES md_font_metrics_test.cpp
    LIBS qlitemd_mdhtml md4c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/mdhtml
    LABELS MARKDOWN
)

add_unit_test(
    NAME test_mdhtml_formula
    SOURCES md_formula_test.cpp
    LIBS qlitemd_mdhtml md4c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/mdhtml
    LABELS MARKDOWN
)
set_tests_properties(test_mdhtml_formula PROPERTIES TIMEOUT 120)

add_unit_test(
    NAME test_mdhtml_mermaid
    SOURCES md_mermaid_test.cpp
    LIBS qlitemd_mdhtml md4c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/mdhtml
    LABELS MARKDOWN
)
set_tests_properties(test_mdhtml_mermaid PROPERTIES TIMEOUT 120)
