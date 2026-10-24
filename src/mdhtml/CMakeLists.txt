set(MDHTML_TARGET qlitemd_mdhtml)

# The bundled highlights queries are text resources owned by the core, so
# they are embedded as generated C++ instead of Qt's resource system.
set(MDHTML_TS_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/tree-sitter-lang")
set(MDHTML_QUERY_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/md_queries_generated.cpp")
set(MDHTML_LANGUAGES
    bash batch c cpp css go html java javascript json python rust)
set(MDHTML_QUERY_FILES "")
foreach(language IN LISTS MDHTML_LANGUAGES)
    list(APPEND MDHTML_QUERY_FILES
        "${MDHTML_TS_ROOT}/${language}/queries/highlights.scm")
endforeach()
add_custom_command(
    OUTPUT "${MDHTML_QUERY_SOURCE}"
    COMMAND ${CMAKE_COMMAND}
        "-DQL_TS_LANG_ROOT=${MDHTML_TS_ROOT}"
        "-DQL_OUTPUT=${MDHTML_QUERY_SOURCE}"
        -P "${CMAKE_SOURCE_DIR}/cmake/embed_mdhtml_queries.cmake"
    DEPENDS ${MDHTML_QUERY_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/embed_mdhtml_queries.cmake"
    COMMENT "Embedding tree-sitter highlight queries"
    VERBATIM
)

add_library(${MDHTML_TARGET} STATIC
    md_assembler.cpp
    md_assembler.h
    md_font_metrics.cpp
    md_font_metrics.h
    md_formula.cpp
    md_formula.h
    md_hash.cpp
    md_hash.h
    md_highlight.cpp
    md_highlight.h
    md_mermaid.cpp
    md_mermaid.h
    md_parser.cpp
    md_parser.h
    md_queries.h
    md_svg.cpp
    md_svg.h
    md_text.cpp
    md_text.h
    md_types.h
    ${MDHTML_QUERY_SOURCE}
)

target_include_directories(${MDHTML_TARGET}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(${MDHTML_TARGET}
    PRIVATE
    md4c
    tree_sitter_core
    tree_sitter_grammars
)

# Native SVG backends stay optional: other platforms fall back to QuickJS
# (not ported yet), and the core must still build without the native libs.
if(QLITEMD_FORMULA_BACKEND STREQUAL "ratex" AND TARGET qlitemd::ratex)
    target_link_libraries(${MDHTML_TARGET} PRIVATE qlitemd::ratex)
    target_compile_definitions(${MDHTML_TARGET} PUBLIC
        QLITEMD_MDHTML_FORMULA_RATEX=1)
endif()
if(QLITEMD_MERMAID_BACKEND STREQUAL "merman" AND TARGET qlitemd::merman)
    target_link_libraries(${MDHTML_TARGET} PRIVATE qlitemd::merman)
    target_compile_definitions(${MDHTML_TARGET} PUBLIC
        QLITEMD_MDHTML_MERMAID_MERMAN=1)
endif()
if(TARGET qlitemd_runtime_dlls)
    add_dependencies(${MDHTML_TARGET} qlitemd_runtime_dlls)
endif()
target_compile_features(${MDHTML_TARGET} PUBLIC cxx_std_17)
set_target_properties(${MDHTML_TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(${MDHTML_TARGET} PRIVATE /utf-8 /W4 /WX- /EHsc)
endif()

add_library(qlitemd::mdhtml ALIAS ${MDHTML_TARGET})
