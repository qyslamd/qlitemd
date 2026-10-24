cmake_minimum_required(VERSION 3.19)

project(qlitemd VERSION 0.0.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(APP_TARGET_PREFIX qlitemd)

# 原生 .so 与可执行文件放在同一目录（见 qlitemd_runtime_dlls），
# 用 $ORIGIN 让 Linux 加载器从可执行文件所在目录解析依赖
if(NOT WIN32)
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    set(CMAKE_BUILD_RPATH "$ORIGIN")
endif()

if(MSVC)
    # Keep the runtime consistent with Qt's /MD binaries and make the
    # standard CRT import libraries available to third-party C targets.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
    add_compile_definitions(NOMINMAX)
    add_compile_options(/utf-8)
endif()

# Third-party projects do not ship install rules in this repository. CMake
# still emits include() lines for their build directories, so leave a small
# placeholder instead of failing during `cmake --install`.
function(qlitemd_skip_subdir_install build_subdir)
    set(_install_script
        "${CMAKE_BINARY_DIR}/${build_subdir}/cmake_install.cmake")
    if(NOT EXISTS "${_install_script}")
        file(WRITE "${_install_script}"
            "# qlitemd: third-party install rules are disabled\n")
    endif()
endfunction()

option(QLITEMD_BUILD_TESTS "Build qlitemd tests" ON)
option(QLITEMD_BUILD_MD_VIEWER "Build the md_viewer example" ON)

set(QLITEMD_SVG_BACKEND "resvg" CACHE STRING "SVG rasterizer: resvg or qtsvg")
set_property(CACHE QLITEMD_SVG_BACKEND PROPERTY STRINGS resvg qtsvg)
set(QLITEMD_FORMULA_BACKEND "ratex" CACHE STRING
    "Formula backend: ratex (native FFI) or quickjs (MathJax)")
set_property(CACHE QLITEMD_FORMULA_BACKEND PROPERTY STRINGS ratex quickjs)
set(QLITEMD_MERMAID_BACKEND "merman" CACHE STRING
    "Mermaid backend: merman or quickjs")
set_property(CACHE QLITEMD_MERMAID_BACKEND PROPERTY STRINGS merman quickjs)
if(NOT QLITEMD_SVG_BACKEND MATCHES "^(resvg|qtsvg)$")
    message(FATAL_ERROR "QLITEMD_SVG_BACKEND must be resvg or qtsvg")
endif()
if(NOT QLITEMD_FORMULA_BACKEND MATCHES "^(ratex|quickjs)$")
    message(FATAL_ERROR "QLITEMD_FORMULA_BACKEND must be ratex or quickjs")
endif()
if(NOT QLITEMD_MERMAID_BACKEND MATCHES "^(merman|quickjs)$")
    message(FATAL_ERROR "QLITEMD_MERMAID_BACKEND must be merman or quickjs")
endif()

# 原生库目录按「平台-架构」组织：win-x64 / win-x86 / linux-x64 / linux-arm64
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _qlitemd_processor)
if(_qlitemd_processor MATCHES "^(aarch64|arm64)$")
    set(QLITEMD_ARCH arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QLITEMD_ARCH x64)
else()
    set(QLITEMD_ARCH x86)
endif()
if(WIN32)
    set(QLITEMD_PLATFORM_DIR "win-${QLITEMD_ARCH}")
else()
    set(QLITEMD_PLATFORM_DIR "linux-${QLITEMD_ARCH}")
endif()

set(_qlitemd_resvg_dir "${CMAKE_SOURCE_DIR}/3rdparty/resvg")
set(_qlitemd_merman_dir "${CMAKE_SOURCE_DIR}/3rdparty/merman")
set(_qlitemd_ratex_dir "${CMAKE_SOURCE_DIR}/3rdparty/ratex")

# 本平台的原生产物文件名：Windows 是 <name>.dll，Linux 是 lib<name>.so
function(qlitemd_native_lib_name out_var lib_name)
    if(WIN32)
        set(${out_var} "${lib_name}.dll" PARENT_SCOPE)
    else()
        set(${out_var} "lib${lib_name}.so" PARENT_SCOPE)
    endif()
endfunction()
function(qlitemd_native_available out_var dir lib_name)
    qlitemd_native_lib_name(_file_name "${lib_name}")
    if(EXISTS "${dir}/bin/${QLITEMD_PLATFORM_DIR}/${_file_name}")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

qlitemd_native_available(_qlitemd_has_resvg "${_qlitemd_resvg_dir}" resvg)
qlitemd_native_available(_qlitemd_has_merman "${_qlitemd_merman_dir}" merman_ffi)
qlitemd_native_available(_qlitemd_has_ratex "${_qlitemd_ratex_dir}" ratex_ffi)

# 请求了本平台缺产物的后端时回退到 quickjs/qtsvg，保证配置总能成功
if(QLITEMD_FORMULA_BACKEND STREQUAL "ratex" AND NOT _qlitemd_has_ratex)
    message(STATUS
        "ratex binary missing for ${QLITEMD_PLATFORM_DIR}; using quickjs")
    set(QLITEMD_FORMULA_BACKEND quickjs)
endif()
if(QLITEMD_MERMAID_BACKEND STREQUAL "merman" AND NOT _qlitemd_has_merman)
    message(STATUS
        "merman binary missing for ${QLITEMD_PLATFORM_DIR}; using quickjs")
    set(QLITEMD_MERMAID_BACKEND quickjs)
endif()
if(QLITEMD_SVG_BACKEND STREQUAL "resvg" AND NOT _qlitemd_has_resvg)
    message(STATUS
        "resvg binary missing for ${QLITEMD_PLATFORM_DIR}; using qtsvg")
    set(QLITEMD_SVG_BACKEND qtsvg)
endif()
if(QLITEMD_MERMAID_BACKEND STREQUAL "merman"
   AND NOT QLITEMD_SVG_BACKEND STREQUAL "resvg")
    message(FATAL_ERROR
        "merman backend emits resvg-safe SVG and requires QLITEMD_SVG_BACKEND=resvg")
endif()

if(QLITEMD_FORMULA_BACKEND STREQUAL "quickjs"
   OR QLITEMD_MERMAID_BACKEND STREQUAL "quickjs")
    set(QLITEMD_NEEDS_QUICKJS TRUE)
else()
    set(QLITEMD_NEEDS_QUICKJS FALSE)
endif()

# 日志自报家门：CI 与本地排查不用反推缓存里到底配了哪个后端
message(STATUS
    "qlitemd backends: SVG=${QLITEMD_SVG_BACKEND}, "
    "Formula=${QLITEMD_FORMULA_BACKEND}, "
    "Mermaid=${QLITEMD_MERMAID_BACKEND}, arch=${QLITEMD_ARCH}")

# Prebuilt native backends. Keep them as imported targets so the rest of the
# build never needs Rust; upgrade instructions live in their VERSION.md files.
# 只有当前构建确实选中的后端才需要导入与拷贝。
set(_qlitemd_runtime_libs "")
if(QLITEMD_SVG_BACKEND STREQUAL "resvg")
    qlitemd_native_lib_name(_resvg_file resvg)
    add_library(qlitemd_resvg SHARED IMPORTED)
    set_target_properties(qlitemd_resvg PROPERTIES
        IMPORTED_LOCATION
            "${_qlitemd_resvg_dir}/bin/${QLITEMD_PLATFORM_DIR}/${_resvg_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${_qlitemd_resvg_dir}/include")
    if(WIN32)
        set_target_properties(qlitemd_resvg PROPERTIES
            IMPORTED_IMPLIB
                "${_qlitemd_resvg_dir}/bin/${QLITEMD_PLATFORM_DIR}/resvg.dll.lib")
    endif()
    add_library(qlitemd::resvg ALIAS qlitemd_resvg)
    list(APPEND _qlitemd_runtime_libs
        "${_qlitemd_resvg_dir}/bin/${QLITEMD_PLATFORM_DIR}/${_resvg_file}")
endif()

if(QLITEMD_MERMAID_BACKEND STREQUAL "merman")
    qlitemd_native_lib_name(_merman_file merman_ffi)
    add_library(qlitemd_merman SHARED IMPORTED)
    set_target_properties(qlitemd_merman PROPERTIES
        IMPORTED_LOCATION
            "${_qlitemd_merman_dir}/bin/${QLITEMD_PLATFORM_DIR}/${_merman_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${_qlitemd_merman_dir}/include")
    if(WIN32)
        set_target_properties(qlitemd_merman PROPERTIES
            IMPORTED_IMPLIB
                "${_qlitemd_merman_dir}/bin/${QLITEMD_PLATFORM_DIR}/merman_ffi.dll.lib")
    endif()
    add_library(qlitemd::merman ALIAS qlitemd_merman)
    list(APPEND _qlitemd_runtime_libs
        "${_qlitemd_merman_dir}/bin/${QLITEMD_PLATFORM_DIR}/${_merman_file}")
endif()

if(QLITEMD_FORMULA_BACKEND STREQUAL "ratex")
    qlitemd_native_lib_name(_ratex_file ratex_ffi)
    add_library(qlitemd_ratex SHARED IMPORTED)
    set_target_properties(qlitemd_ratex PROPERTIES
        IMPORTED_LOCATION
            "${_qlitemd_ratex_dir}/bin/${QLITEMD_PLATFORM_DIR}/${_ratex_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${_qlitemd_ratex_dir}/include")
    if(WIN32)
        set_target_properties(qlitemd_ratex PROPERTIES
            IMPORTED_IMPLIB
                "${_qlitemd_ratex_dir}/bin/${QLITEMD_PLATFORM_DIR}/ratex_ffi.dll.lib")
    endif()
    add_library(qlitemd::ratex ALIAS qlitemd_ratex)
    list(APPEND _qlitemd_runtime_libs
        "${_qlitemd_ratex_dir}/bin/${QLITEMD_PLATFORM_DIR}/${_ratex_file}")
endif()

# Tests and examples use different runtime directories; stage the selected
# native libraries next to each so ctest and md_viewer start without a
# manually adjusted PATH/LD_LIBRARY_PATH.
if(_qlitemd_runtime_libs)
    add_custom_target(qlitemd_runtime_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/tests"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_qlitemd_runtime_libs} "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_qlitemd_runtime_libs} "${CMAKE_BINARY_DIR}/bin/tests"
        COMMENT "Stage native backend libraries"
        VERBATIM)
endif()

set(QLITEMD_QT_MAJOR 5 CACHE STRING "Qt major version (5 or 6)")
set(QT_VERSION_MAJOR ${QLITEMD_QT_MAJOR})
if(QLITEMD_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS
        Core Gui Widgets Svg Network PrintSupport Test)
else()
    find_package(Qt5 5.12 REQUIRED COMPONENTS
        Core Gui Widgets Svg Network PrintSupport Test)
endif()

include(cmake/run_app.cmake)

# ---------------------------------------------------------------- spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/spdlog)
qlitemd_skip_subdir_install(3rdparty/spdlog)

# ------------------------------------------------------------ litehtml
set(CMAKE_SKIP_INSTALL_RULES TRUE)
add_subdirectory(3rdparty/litehtml "${CMAKE_BINARY_DIR}/litehtml")
unset(CMAKE_SKIP_INSTALL_RULES)
qlitemd_skip_subdir_install(litehtml)
target_include_directories(litehtml SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/3rdparty/litehtml/include"
    "${CMAKE_SOURCE_DIR}/3rdparty/litehtml/src")

# ----------------------------------------------------------- qlitehtml
set(QLITEHTML_LIBRARY_TYPE STATIC)
set(CMAKE_SKIP_INSTALL_RULES TRUE)
add_subdirectory(3rdparty/qlitehtml/src
                 "${CMAKE_BINARY_DIR}/qlitehtml")
unset(CMAKE_SKIP_INSTALL_RULES)
qlitemd_skip_subdir_install(qlitehtml)
set_target_properties(qlitehtml PROPERTIES AUTOMOC ON)

# --------------------------------------------------------------- md4c
add_library(md4c STATIC
    "${CMAKE_SOURCE_DIR}/3rdparty/md4c/src/md4c.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/md4c/src/md4c-html.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/md4c/src/entity.c")
target_include_directories(md4c PUBLIC
    "${CMAKE_SOURCE_DIR}/3rdparty/md4c/src")
set_target_properties(md4c PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# ------------------------------------------------------------- QuickJS
# 只有选了 quickjs 后端（公式或 Mermaid）才编译 QuickJS 与 JS 载荷
if(QLITEMD_NEEDS_QUICKJS)
    set(QJS_ENABLE_INSTALL OFF CACHE BOOL "Do not install QuickJS" FORCE)
    add_subdirectory(3rdparty/quickjs-ng)
    qlitemd_skip_subdir_install(3rdparty/quickjs-ng)
endif()

# ------------------------------------------------- tree-sitter (native)
add_subdirectory(3rdparty/tree-sitter)
qlitemd_skip_subdir_install(3rdparty/tree-sitter)

# ------------------------------------------------------ viewer UI deps
if(QLITEMD_BUILD_MD_VIEWER)
set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/qwindowkit)
qlitemd_skip_subdir_install(3rdparty/qwindowkit)
if(NOT TARGET QWindowKit::Widgets)
    message(FATAL_ERROR "QWindowKit::Widgets target is missing")
endif()

# -------------------------------------------------------- FluentUI
set(BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(BUILD_LIBRARY ON CACHE BOOL "" FORCE)
set(BUILD_FRAMELESS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_QTCREATOR_PLUGIN OFF CACHE BOOL "" FORCE)
# 只编 md_viewer 用到的 ExWidgets；频谱控件依赖 kissfft，整块裁掉
set(EXWIDGETS_MINIMAL ON CACHE BOOL "" FORCE)
set(EXWIDGETS_ENABLE_SPECTRUM OFF CACHE BOOL "" FORCE)
set(CMAKE_SKIP_INSTALL_RULES TRUE)
add_subdirectory(3rdparty/fluentui)
qlitemd_skip_subdir_install(3rdparty/fluentui)
unset(CMAKE_SKIP_INSTALL_RULES)
endif()

# ------------------------------------------------------------ modules
add_subdirectory(src/logging)
qlitemd_skip_subdir_install(src/logging)

add_library(qlitemd_single_instance STATIC
    src/common/single_instance.cpp
    src/common/single_instance.h)
set_target_properties(qlitemd_single_instance PROPERTIES AUTOMOC ON)
target_include_directories(qlitemd_single_instance PUBLIC
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(qlitemd_single_instance PUBLIC
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network qlitemd_log)
if(WIN32)
    target_link_libraries(qlitemd_single_instance PRIVATE user32)
endif()

add_subdirectory(src/markdown)

set(QLITEMD_PUBLIC_HEADERS
    src/markdown/heading_ids.h
    src/markdown/markdown_outline_widget.h
    src/markdown/markdown_render_tasks.h
    src/markdown/markdown_theme.h
    src/markdown/markdown_to_html.h
    src/markdown/markdown_types.h
    src/markdown/markdown_view.h)

# The public library only exposes qlitemd headers; qlitehtml, litehtml, md4c
# and QuickJS stay implementation details. Their archives are installed because
# qlitemd_markdown is a static library and consumers must link them.
# All of these targets are static archives. Install the files directly so the
# vendored targets' PUBLIC_HEADER paths are not resolved against this source
# directory; only qlitemd's public headers are part of the SDK.
install(FILES
    "$<TARGET_FILE:qlitemd_markdown>"
    "$<TARGET_FILE:qlitehtml>"
    "$<TARGET_FILE:litehtml>"
    "$<TARGET_FILE:gumbo>"
    "$<TARGET_FILE:md4c>"
    "$<TARGET_FILE:tree_sitter_core>"
    "$<TARGET_FILE:tree_sitter_grammars>"
    DESTINATION lib
    COMPONENT Development)
if(QLITEMD_NEEDS_QUICKJS)
    install(FILES "$<TARGET_FILE:qjs>"
        DESTINATION lib COMPONENT Development)
endif()

install(FILES ${QLITEMD_PUBLIC_HEADERS}
    DESTINATION include/qlitemd
    COMPONENT Development)

include(CMakePackageConfigHelpers)
set(QLITEMD_CMAKE_INSTALL_DIR lib/cmake/qlitemd)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qlitemdConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/qlitemdConfig.cmake"
    INSTALL_DESTINATION ${QLITEMD_CMAKE_INSTALL_DIR})
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/qlitemdConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/qlitemdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/qlitemdConfigVersion.cmake"
    DESTINATION ${QLITEMD_CMAKE_INSTALL_DIR}
    COMPONENT Development)

install(FILES LICENSE NOTICE README.md README.zh-CN.md
    DESTINATION .
    COMPONENT Licenses)
install(FILES 3rdparty/litehtml/LICENSE
    DESTINATION licenses/litehtml COMPONENT Licenses)
install(FILES 3rdparty/litehtml/src/gumbo/LICENSE
    DESTINATION licenses/gumbo COMPONENT Licenses)
install(FILES 3rdparty/md4c/LICENSE.md
    DESTINATION licenses/md4c COMPONENT Licenses)
if(NOT WIN32)
    install(FILES 3rdparty/quickjs-ng/LICENSE
        DESTINATION licenses/quickjs COMPONENT Licenses)
endif()
install(FILES 3rdparty/spdlog/LICENSE
    DESTINATION licenses/spdlog COMPONENT Licenses)
install(FILES 3rdparty/qwindowkit/LICENSE
    DESTINATION licenses/qwindowkit COMPONENT Licenses)
install(FILES 3rdparty/qmsetup/LICENSE
    DESTINATION licenses/qmsetup COMPONENT Licenses)
install(FILES 3rdparty/fluentui/LICENSE
    DESTINATION licenses/fluentui COMPONENT Licenses)
install(FILES 3rdparty/fluentui/THIRD_PARTY_NOTICES.md
    DESTINATION licenses/fluentui COMPONENT Licenses)
install(DIRECTORY 3rdparty/qlitehtml/LICENSES
    DESTINATION licenses/qlitehtml COMPONENT Licenses)
install(FILES 3rdparty/resvg/LICENSE-APACHE 3rdparty/resvg/LICENSE-MIT
    3rdparty/resvg/VERSION.md
    DESTINATION licenses/resvg COMPONENT Licenses)
install(FILES 3rdparty/merman/LICENSE-APACHE 3rdparty/merman/LICENSE-MIT
    3rdparty/merman/THIRD_PARTY_NOTICES.md 3rdparty/merman/VERSION.md
    DESTINATION licenses/merman COMPONENT Licenses)
install(FILES 3rdparty/ratex/LICENSE 3rdparty/ratex/VERSION.md
    DESTINATION licenses/ratex COMPONENT Licenses)
# 选中的原生后端：Windows 的 DLL 进 bin、导入库进 lib；Linux 的 .so
# 进 lib（Runtime 组件），消费方通过 config 里的绝对路径链接
if(_qlitemd_runtime_libs)
    if(WIN32)
        install(FILES ${_qlitemd_runtime_libs}
            DESTINATION bin COMPONENT Runtime)
    else()
        install(FILES ${_qlitemd_runtime_libs}
            DESTINATION lib COMPONENT Runtime)
    endif()
endif()
if(WIN32)
    set(_qlitemd_native_import_libs "")
    if(QLITEMD_SVG_BACKEND STREQUAL "resvg")
        list(APPEND _qlitemd_native_import_libs
            "${_qlitemd_resvg_dir}/bin/${QLITEMD_PLATFORM_DIR}/resvg.dll.lib")
    endif()
    if(QLITEMD_MERMAID_BACKEND STREQUAL "merman")
        list(APPEND _qlitemd_native_import_libs
            "${_qlitemd_merman_dir}/bin/${QLITEMD_PLATFORM_DIR}/merman_ffi.dll.lib")
    endif()
    if(QLITEMD_FORMULA_BACKEND STREQUAL "ratex")
        list(APPEND _qlitemd_native_import_libs
            "${_qlitemd_ratex_dir}/bin/${QLITEMD_PLATFORM_DIR}/ratex_ffi.dll.lib")
    endif()
    if(_qlitemd_native_import_libs)
        install(FILES ${_qlitemd_native_import_libs}
            DESTINATION lib COMPONENT Development)
    endif()
endif()
install(FILES licenses/mathjax/LICENSE
    DESTINATION licenses/mathjax COMPONENT Licenses)
install(FILES licenses/mermaid/LICENSE
    DESTINATION licenses/mermaid COMPONENT Licenses)
install(FILES licenses/mermaidx/LICENSE
    DESTINATION licenses/mermaidx COMPONENT Licenses)

# --------------------------------------------------------------- tests
if(QLITEMD_BUILD_TESTS)
    enable_testing()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/googletest)
    qlitemd_skip_subdir_install(3rdparty/googletest)
    include(cmake/test_helpers.cmake)
    add_subdirectory(tests/markdown)
    qlitemd_skip_subdir_install(tests/markdown)
    add_subdirectory(tests/md_viewer)
    qlitemd_skip_subdir_install(tests/md_viewer)
endif()

# ------------------------------------------------------------- viewer
if(QLITEMD_BUILD_MD_VIEWER)
    add_subdirectory(examples/md_viewer)
endif()
