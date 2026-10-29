set(MDHTML_TARGET qlitemd_mdhtml)

# The bundled highlights queries are text resources owned by the core, so
# they are embedded as generated C++ instead of Qt's resource system.
set(MDHTML_TS_ROOT "${PROJECT_SOURCE_DIR}/3rdparty/tree-sitter-lang")
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
        -P "${PROJECT_SOURCE_DIR}/cmake/embed_mdhtml_queries.cmake"
    DEPENDS ${MDHTML_QUERY_FILES}
        "${PROJECT_SOURCE_DIR}/cmake/embed_mdhtml_queries.cmake"
    COMMENT "Embedding tree-sitter highlight queries"
    VERBATIM
)

# QuickJS is always part of the build: mermaid.js is the mermaid fallback
# and the MathJax formula builds use it too. Payloads are qjsc bytecode,
# generated per build directory just like the Qt module's copy.
set(MDHTML_QUICKJS_SOURCES "")
if(NOT TARGET qjsc)
    message(FATAL_ERROR
        "qlitemd_mdhtml 需要 qjsc：请确认 quickjs-ng 在 src/mdhtml 之前加入")
endif()
set(MDHTML_BYTECODE_DIR "${CMAKE_CURRENT_BINARY_DIR}/bytecode")
set(MDHTML_BYTECODE_SRCS "")
foreach(entry "md_mj:mathjax-svg.min.js"
              "md_mm:mermaid.js"
              "md_ds:dom_shim.js"
              "md_pb:path_bbox.js")
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 symbol)
    list(GET parts 1 filename)
    set(js_file "${PROJECT_SOURCE_DIR}/resources/${filename}")
    set(c_file "${MDHTML_BYTECODE_DIR}/${symbol}.c")
    add_custom_command(
        OUTPUT "${c_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MDHTML_BYTECODE_DIR}"
        COMMAND "$<TARGET_FILE:qjsc>" -C -s -s -N ${symbol}
                -o "${c_file}" "${js_file}"
        DEPENDS "${js_file}" qjsc
        COMMENT "qjsc(mdhtml): ${filename} -> ${symbol}.c"
        VERBATIM
    )
    list(APPEND MDHTML_BYTECODE_SRCS "${c_file}")
endforeach()
list(APPEND MDHTML_QUICKJS_SOURCES
    md_bytecode.h
    md_js_runtime.cpp
    md_js_runtime.h
    ${MDHTML_BYTECODE_SRCS})

add_library(${MDHTML_TARGET} STATIC
    md_assembler.cpp
    md_assembler.h
    md_callout.cpp
    md_callout.h
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
    md_pipeline.cpp
    md_pipeline.h
    md_queries.h
    md_svg.cpp
    md_svg.h
    md_text.cpp
    md_text.h
    md_types.h
    ${MDHTML_QUERY_SOURCE}
    ${MDHTML_QUICKJS_SOURCES}
)

target_include_directories(${MDHTML_TARGET}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
find_package(Threads REQUIRED)
target_link_libraries(${MDHTML_TARGET}
    PRIVATE
    md4c
    tree_sitter_core
    tree_sitter_grammars
    Threads::Threads
)

# Formula compiles both backends: RaTeX when the platform provides the
# prebuilt library, plus MathJax in QuickJS everywhere.
if(TARGET qlitemd::ratex)
    target_link_libraries(${MDHTML_TARGET} PRIVATE qlitemd::ratex)
    target_compile_definitions(${MDHTML_TARGET} PUBLIC
        QLITEMD_MDHTML_FORMULA_RATEX=1)
endif()
if(TARGET qlitemd::merman)
    target_link_libraries(${MDHTML_TARGET} PRIVATE qlitemd::merman)
    target_compile_definitions(${MDHTML_TARGET} PUBLIC
        QLITEMD_MDHTML_MERMAID_MERMAN=1)
endif()
target_link_libraries(${MDHTML_TARGET} PRIVATE qjs)
target_compile_definitions(${MDHTML_TARGET} PUBLIC
    QLITEMD_MDHTML_QUICKJS=1)
if(TARGET qlitemd_runtime_dlls)
    add_dependencies(${MDHTML_TARGET} qlitemd_runtime_dlls)
endif()
target_compile_features(${MDHTML_TARGET} PUBLIC cxx_std_17)
set_target_properties(${MDHTML_TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(${MDHTML_TARGET} PRIVATE /utf-8 /W4 /WX- /EHsc)
endif()

add_library(qlitemd::mdhtml ALIAS ${MDHTML_TARGET})

# Shared C ABI library for non-C++ hosts (C#, Rust, ...). It compiles the C
# API translation unit itself so the exported symbols get the API macros;
# the implementation links the static C++ core.
add_library(qlitemd_mdhtml_c SHARED
    mdhtml_c.cpp
    mdhtml_c.h
)
target_link_libraries(qlitemd_mdhtml_c PRIVATE ${MDHTML_TARGET})
target_compile_features(qlitemd_mdhtml_c PRIVATE cxx_std_17)
target_compile_definitions(qlitemd_mdhtml_c PRIVATE
    MDHTML_SHARED
    MDHTML_BUILD
    "MDHTML_VERSION_STRING=\"${PROJECT_VERSION}\"")
target_compile_definitions(qlitemd_mdhtml_c INTERFACE MDHTML_SHARED)
set_target_properties(qlitemd_mdhtml_c PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    OUTPUT_NAME qlitemd_mdhtml_c
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
)
if(TARGET qlitemd_runtime_dlls)
    add_dependencies(qlitemd_mdhtml_c qlitemd_runtime_dlls)
endif()
if(MSVC)
    target_compile_options(qlitemd_mdhtml_c PRIVATE /utf-8 /W4 /WX- /EHsc)
endif()
add_library(qlitemd::mdhtml_c ALIAS qlitemd_mdhtml_c)
