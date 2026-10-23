set(TARGET_NAME ${APP_TARGET_PREFIX}_markdown)

set(TARGET_SRCS
    code_highlighter.cpp
    code_highlighter.h
    formula_renderer.cpp
    formula_renderer.h
    js_runtime.cpp
    js_runtime.h
    markdown_bytecode.h
    markdown_engine_state.h
    markdown_outline_widget.cpp
    markdown_outline_widget.h
    markdown_render_context.cpp
    markdown_render_context.h
    markdown_render_context_internal.h
    markdown_render_tasks.cpp
    markdown_render_tasks.h
    markdown_theme.cpp
    markdown_theme.h
    markdown_types.h
    html_escape.h
    merman_renderer.h
    mermaid_renderer.cpp
    mermaid_renderer.h
    mermaid_svg_cleanup.cpp
    mermaid_svg_cleanup.h
    svg_rasterizer.cpp
    svg_rasterizer.h
    markdown_to_html.cpp
    markdown_to_html.h
    markdown_view.cpp
    markdown_view.h
    render_cache.cpp
    render_cache.h
    render_diagnostics.cpp
    render_diagnostics.h
    render_service.cpp
    render_service.h
)

# 公式走 SVG→PNG（QSvgRenderer 在 Gui 里，Svg 模块提供 svg 支持）
if(NOT TARGET Qt${QT_VERSION_MAJOR}::Svg)
    message(FATAL_ERROR
        "Qt Svg 没找到：公式渲染依赖 QSvgRenderer（见 src/markdown/formula_renderer.cpp）")
endif()

set(MARKDOWN_RESOURCES markdown_res.qrc)
if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_resources(MARKDOWN_RESOURCES_CPP ${MARKDOWN_RESOURCES})
else()
    qt5_add_resources(MARKDOWN_RESOURCES_CPP ${MARKDOWN_RESOURCES})
endif()

# -----------------------------------------------------------------------------
# qjsc：构建期把五份 JS 载荷预编译成字节码 C 数组（渲染器经
# markdown_bytecode.h 加载，不再从 qrc 读源码）。冷加载实测数据见
# docs/eval/Markdown渲染qjsc预编译评估.md。
#
# 五份载荷都是 classic script（无行首 import/export），-C 出字节码数组；
# -s -s 剥源码与调试信息（排查靠 qWarning 消息与 dump 产物，不靠 JS 行号）；
# -N 指定 C 变量名。只认 in-tree qjsc target：系统 PATH 里的异版本 qjsc
# 产出的字节码 BC_VERSION 不匹配，会拖到运行时才永久 FailLoad，必须在这里
# configure 期拦住。字节码与 QuickJS 版本/位数/编译开关绑定，各构建目录
# 各自生成、不入库；载荷文件变化时 DEPENDS 自动重编
# -----------------------------------------------------------------------------
if(NOT TARGET qjsc)
    message(FATAL_ERROR
        "qjsc 目标不存在：markdown 的 JS 载荷需要在构建期用 qjsc 预编译成"
        "字节码（本模块链接它的产物）。确认 3rdparty/quickjs-ng 的"
        " add_subdirectory 先于 src/markdown。")
endif()
set(QJSC_CMD "$<TARGET_FILE:qjsc>")
set(MARKDOWN_BYTECODE_DIR "${CMAKE_CURRENT_BINARY_DIR}/bytecode")
set(MARKDOWN_BYTECODE_SRCS "")
foreach(entry "md_hl:highlight.min.js"
              "md_mj:mathjax-svg.min.js"
              "md_mm:mermaid.js"
              "md_ds:dom_shim.js"
              "md_pb:path_bbox.js")
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 symbol)
    list(GET parts 1 filename)
    set(js_file "${CMAKE_SOURCE_DIR}/resources/${filename}")
    set(c_file "${MARKDOWN_BYTECODE_DIR}/${symbol}.c")
    add_custom_command(
        OUTPUT "${c_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MARKDOWN_BYTECODE_DIR}"
        COMMAND "${QJSC_CMD}" -C -s -s -N ${symbol}
                -o "${c_file}" "${js_file}"
        DEPENDS "${js_file}" qjsc
        COMMENT "qjsc: ${filename} -> ${symbol}.c"
        VERBATIM
    )
    list(APPEND MARKDOWN_BYTECODE_SRCS "${c_file}")
endforeach()

if(WIN32)
    list(APPEND TARGET_SRCS merman_renderer.cpp)
else()
    list(APPEND TARGET_SRCS merman_renderer_stub.cpp)
endif()

add_library(${TARGET_NAME} STATIC
    ${TARGET_SRCS}
    ${MARKDOWN_BYTECODE_SRCS}
    ${MARKDOWN_RESOURCES_CPP})

set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_STANDARD 17
    C_STANDARD 11
    AUTOMOC ON
    AUTOUIC ON
)

# PUBLIC：公开头用到 Qt 类型，宿主得连着拿到。
# qlitehtml / md4c 只在 .cpp 里出现，链接与头文件路径都不外传
target_link_libraries(${TARGET_NAME}
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
    PRIVATE
    qlitehtml
    md4c
    qjs            # 公式与高亮：在 QuickJS 里跑 MathJax/highlight.js
    Qt${QT_VERSION_MAJOR}::Svg
)
if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE
        qlitemd::resvg
        qlitemd::merman)
endif()

target_include_directories(${TARGET_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(QLITEMD_SVG_BACKEND STREQUAL "resvg")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        QLITEMD_SVG_BACKEND_RESVG=1)
endif()
if(QLITEMD_MERMAID_BACKEND STREQUAL "merman")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        QLITEMD_MERMAID_BACKEND_MERMAN=1)
endif()
if(WIN32)
    add_dependencies(${TARGET_NAME} qlitemd_runtime_dlls)
endif()

if(MSVC)
    # Public headers contain UTF-8 comments; consumers need the same source
    # charset as the library itself.
    target_compile_options(${TARGET_NAME} PUBLIC /utf-8)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX- /EHsc)

    # 线程栈放大：MathJax 在 QuickJS 里解析递归深，formula_renderer.cpp 里
    # 把 JS 配额放到 16MB，线程栈要更宽裕（JS 配额是软上限、线程栈是硬的，
    # 反过来会砸穿原生栈直接崩）。
    # 别贪大：32 位进程的地址空间只有 2GB，主线程栈 reserve 太大会挤占
    # 地址空间（实测 128MB 时线程创建失败）；32MB 在 x86/x64 上都可用。
    # 走 INTERFACE 是因为真正需要大栈的是"跑 JS 的那个进程"，即链接本模块的
    # 可执行文件；GCC 不需要（Linux 靠默认栈即可）
    target_link_options(${TARGET_NAME} INTERFACE "/STACK:33554432")
endif()
