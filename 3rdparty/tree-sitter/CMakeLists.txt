# tree-sitter 核心与 12 份 grammar 的静态库。全部纯 C 源码，MSVC/Linux
# 原生编译，无预编译二进制。语言集合与版本见 tree-sitter-lang/VERSION.md。

set(TS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TS_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# lib.c 是聚合编译单元（#include 同级全部 .c），只编它一个，别把同级
# .c 再加进目标——会重定义符号
add_library(tree_sitter_core STATIC "${TS_SOURCES}/lib.c")
target_include_directories(tree_sitter_core PUBLIC "${TS_INCLUDE}")
target_include_directories(tree_sitter_core PRIVATE "${TS_SOURCES}")

# 各 grammar 的解析器聚合为一个库：每个 src 目录自带 tree_sitter/parser.h
#（覆盖核心同名头的 LANGUAGE_VERSION），与 demo 验证过的构建方式一致
set(TS_LANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../tree-sitter-lang")
add_library(tree_sitter_grammars STATIC)
target_include_directories(tree_sitter_grammars PRIVATE "${TS_INCLUDE}")

foreach(lang bash batch c cpp css go html java javascript json python rust)
    set(lang_src "${TS_LANG_ROOT}/${lang}/src")
    target_sources(tree_sitter_grammars PRIVATE "${lang_src}/parser.c")
    foreach(scanner scanner.c scanner.cc)
        if(EXISTS "${lang_src}/${scanner}")
            target_sources(tree_sitter_grammars PRIVATE "${lang_src}/${scanner}")
        endif()
    endforeach()
    target_include_directories(tree_sitter_grammars PRIVATE "${lang_src}")
endforeach()

# 第三方源码不追求告警清洁；MSVC 关掉告警避免噪音，Linux 交给 CI
if(MSVC)
    target_compile_options(tree_sitter_core PRIVATE /w)
    target_compile_options(tree_sitter_grammars PRIVATE /w)
endif()
