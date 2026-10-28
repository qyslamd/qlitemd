cmake_minimum_required(VERSION 3.19)

project(qlitemd_qt_widget_minimal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This example is also the add_subdirectory smoke test used by CI. The
# consumer application owns the Qt kit; qlitemd only builds the widget
# component and skips the demo app, tests and install rules.
set(QLITEMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QLITEMD_BUILD_MD_VIEWER OFF CACHE BOOL "" FORCE)
set(QLITEMD_BUILD_QT_WIDGET ON CACHE BOOL "" FORCE)
set(QLITEMD_INSTALL OFF CACHE BOOL "" FORCE)

if(NOT QLITEMD_SOURCE_DIR)
    set(QLITEMD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
get_filename_component(QLITEMD_SOURCE_DIR "${QLITEMD_SOURCE_DIR}" ABSOLUTE)

add_subdirectory("${QLITEMD_SOURCE_DIR}" qlitemd)

add_executable(qlitemd_qt_widget_minimal main.cpp)
target_link_libraries(qlitemd_qt_widget_minimal PRIVATE qlitemd::markdown_view)

if(WIN32)
    target_link_libraries(qlitemd_qt_widget_minimal PRIVATE psapi)
endif()
