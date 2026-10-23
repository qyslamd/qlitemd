cmake_minimum_required(VERSION 3.19)

project(qlitemd VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(APP_TARGET_PREFIX qlitemd)

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
set(QLITEMD_MERMAID_BACKEND "merman" CACHE STRING
    "Mermaid backend: merman or quickjs")
set_property(CACHE QLITEMD_MERMAID_BACKEND PROPERTY STRINGS merman quickjs)
if(NOT QLITEMD_SVG_BACKEND MATCHES "^(resvg|qtsvg)$")
    message(FATAL_ERROR "QLITEMD_SVG_BACKEND must be resvg or qtsvg")
endif()
if(NOT QLITEMD_MERMAID_BACKEND MATCHES "^(merman|quickjs)$")
    message(FATAL_ERROR "QLITEMD_MERMAID_BACKEND must be merman or quickjs")
endif()
if(QLITEMD_MERMAID_BACKEND STREQUAL "merman"
   AND NOT QLITEMD_SVG_BACKEND STREQUAL "resvg")
    message(FATAL_ERROR
        "merman backend emits resvg-safe SVG and requires QLITEMD_SVG_BACKEND=resvg")
endif()

# The native backends are Windows-only in this iteration. Linux/WSL keeps the
# legacy QtSvg/QuickJS path so the existing CI stays usable.
if(NOT WIN32)
    if(QLITEMD_SVG_BACKEND STREQUAL "resvg"
       OR QLITEMD_MERMAID_BACKEND STREQUAL "merman")
        message(STATUS
            "resvg/merman are Windows-only; using qtsvg/quickjs on this platform")
        set(QLITEMD_SVG_BACKEND qtsvg)
        set(QLITEMD_MERMAID_BACKEND quickjs)
    endif()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QLITEMD_ARCH x64)
else()
    set(QLITEMD_ARCH x86)
endif()

# 日志自报家门：CI 与本地排查不用反推缓存里到底配了哪个后端
message(STATUS
    "qlitemd backends: SVG=${QLITEMD_SVG_BACKEND}, "
    "Mermaid=${QLITEMD_MERMAID_BACKEND}, arch=${QLITEMD_ARCH}")

# Prebuilt native backends. Keep them as imported targets so the rest of the
# build never needs Rust; upgrade instructions live in their VERSION.md files.
set(_qlitemd_resvg_dir "${CMAKE_SOURCE_DIR}/3rdparty/resvg")
set(_qlitemd_merman_dir "${CMAKE_SOURCE_DIR}/3rdparty/merman")
if(WIN32)
    foreach(_file
        "${_qlitemd_resvg_dir}/bin/${QLITEMD_ARCH}/resvg.dll"
        "${_qlitemd_resvg_dir}/bin/${QLITEMD_ARCH}/resvg.dll.lib"
        "${_qlitemd_merman_dir}/bin/${QLITEMD_ARCH}/merman_ffi.dll"
        "${_qlitemd_merman_dir}/bin/${QLITEMD_ARCH}/merman_ffi.dll.lib")
        if(NOT EXISTS "${_file}")
            message(FATAL_ERROR "missing prebuilt backend file: ${_file}")
        endif()
    endforeach()

    add_library(qlitemd_resvg SHARED IMPORTED)
    set_target_properties(qlitemd_resvg PROPERTIES
        IMPORTED_LOCATION "${_qlitemd_resvg_dir}/bin/${QLITEMD_ARCH}/resvg.dll"
        IMPORTED_IMPLIB "${_qlitemd_resvg_dir}/bin/${QLITEMD_ARCH}/resvg.dll.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_qlitemd_resvg_dir}/include")
    add_library(qlitemd::resvg ALIAS qlitemd_resvg)

    add_library(qlitemd_merman SHARED IMPORTED)
    set_target_properties(qlitemd_merman PROPERTIES
        IMPORTED_LOCATION "${_qlitemd_merman_dir}/bin/${QLITEMD_ARCH}/merman_ffi.dll"
        IMPORTED_IMPLIB "${_qlitemd_merman_dir}/bin/${QLITEMD_ARCH}/merman_ffi.dll.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_qlitemd_merman_dir}/include")
    add_library(qlitemd::merman ALIAS qlitemd_merman)

    # Tests and examples use different runtime directories; copy both DLLs to
    # each so ctest and md_viewer can start without a manually adjusted PATH.
    add_custom_target(qlitemd_runtime_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/tests"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qlitemd_resvg_dir}/bin/${QLITEMD_ARCH}/resvg.dll"
            "${_qlitemd_merman_dir}/bin/${QLITEMD_ARCH}/merman_ffi.dll"
            "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qlitemd_resvg_dir}/bin/${QLITEMD_ARCH}/resvg.dll"
            "${_qlitemd_merman_dir}/bin/${QLITEMD_ARCH}/merman_ffi.dll"
            "${CMAKE_BINARY_DIR}/bin/tests"
        COMMENT "Copy resvg/merman runtime DLLs"
        VERBATIM)
endif()

set(QLITEMD_QT_MAJOR 5 CACHE STRING "Qt major version (5 or 6)")
set(QT_VERSION_MAJOR ${QLITEMD_QT_MAJOR})
if(QLITEMD_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS
        Core Gui Widgets Svg Network PrintSupport Test)
else()
    find_package(Qt5 5.14 REQUIRED COMPONENTS
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
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "Do not install QuickJS" FORCE)
add_subdirectory(3rdparty/quickjs-ng)
qlitemd_skip_subdir_install(3rdparty/quickjs-ng)

# ------------------------------------------------------ viewer UI deps
if(QLITEMD_BUILD_MD_VIEWER)
if(QLITEMD_QT_MAJOR EQUAL 5 AND Qt5_VERSION VERSION_LESS "5.14")
    message(FATAL_ERROR "QWindowKit requires Qt 5.14 or newer")
endif()
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
    "$<TARGET_FILE:qjs>"
    DESTINATION lib
    COMPONENT Development)

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
install(FILES 3rdparty/quickjs-ng/LICENSE
    DESTINATION licenses/quickjs COMPONENT Licenses)
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
if(WIN32)
    install(FILES
        "3rdparty/resvg/bin/${QLITEMD_ARCH}/resvg.dll"
        "3rdparty/merman/bin/${QLITEMD_ARCH}/merman_ffi.dll"
        DESTINATION bin COMPONENT Runtime)
    install(FILES
        "3rdparty/resvg/bin/${QLITEMD_ARCH}/resvg.dll.lib"
        "3rdparty/merman/bin/${QLITEMD_ARCH}/merman_ffi.dll.lib"
        DESTINATION lib COMPONENT Development)
endif()
install(FILES licenses/mathjax/LICENSE
    DESTINATION licenses/mathjax COMPONENT Licenses)
install(FILES licenses/mermaid/LICENSE
    DESTINATION licenses/mermaid COMPONENT Licenses)
install(FILES licenses/highlightjs/LICENSE
    DESTINATION licenses/highlightjs COMPONENT Licenses)
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
endif()

# ------------------------------------------------------------- viewer
if(QLITEMD_BUILD_MD_VIEWER)
    add_subdirectory(examples/md_viewer)
endif()
