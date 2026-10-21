cmake_minimum_required(VERSION 3.16)

project(Win11Clock VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/wd4273 /utf-8)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)

# Standalone Clock project pulls sibling libraries itself. When built from the
# repository root, respect the parent's BUILD_FRAMELESS / already-created targets.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_filename_component(WINDOW11STYLE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    if(NOT TARGET ExWidgets::ExWidgets)
        add_subdirectory("${WINDOW11STYLE_ROOT}/ExWidgets"
                         "${CMAKE_CURRENT_BINARY_DIR}/dependencies/ExWidgets")
    endif()
    if(NOT TARGET Frameless::Frameless)
        add_subdirectory("${WINDOW11STYLE_ROOT}/frameless"
                         "${CMAKE_CURRENT_BINARY_DIR}/dependencies/frameless")
    endif()
endif()

file(GLOB_RECURSE WIN11_CLOCK_SOURCES
    "src/*.cpp"
    "src/*.h"
    "*.rc"
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(Win11Clock MANUAL_FINALIZATION ${WIN11_CLOCK_SOURCES}
        res.qrc)
else()
    add_executable(Win11Clock ${WIN11_CLOCK_SOURCES})
endif()

target_include_directories(Win11Clock PRIVATE src)

target_link_libraries(Win11Clock PRIVATE
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)

if(NOT WIN32 AND TARGET FluentUI3Style)
    target_link_libraries(Win11Clock PRIVATE FluentUI3Style)
    target_compile_definitions(Win11Clock PRIVATE HAS_FLUENTUI3_STYLE_LIB)
endif()

if(TARGET Frameless::Frameless)
    target_compile_definitions(Win11Clock PRIVATE WIN11CLOCK_ENABLE_FRAMELESS)
    target_link_libraries(Win11Clock PRIVATE Frameless::Frameless)
endif()

set_target_properties(Win11Clock PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    DEBUG_POSTFIX d
)

if(NOT WIN32)
    set_target_properties(Win11Clock PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_finalize_executable(Win11Clock)
endif()
