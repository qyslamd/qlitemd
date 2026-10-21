cmake_minimum_required(VERSION 3.19)

project(qlitemd VERSION 0.1.0 LANGUAGES C CXX)

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

set(QLITEMD_QT_MAJOR 5 CACHE STRING "Qt major version (5 or 6)")
set(QT_VERSION_MAJOR ${QLITEMD_QT_MAJOR})
if(QLITEMD_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS
        Core Gui Widgets Svg Network PrintSupport Test)
else()
    find_package(Qt5 5.14 REQUIRED COMPONENTS
        Core Gui Widgets Svg Network PrintSupport Test)
endif()

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
add_subdirectory(3rdparty/quickjs)
qlitemd_skip_subdir_install(3rdparty/quickjs)

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

install(TARGETS qlitemd_markdown ARCHIVE DESTINATION lib)
install(DIRECTORY src/markdown/
    DESTINATION include/qlitemd
    FILES_MATCHING PATTERN "*.h")
install(FILES LICENSE NOTICE README.md README.zh-CN.md DESTINATION .)

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
