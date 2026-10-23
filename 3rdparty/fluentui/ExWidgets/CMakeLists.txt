# qlitemd 本地补丁（patches/FluentUIStyle/0002-exwidgets-trim.patch）：
# - EXWIDGETS_ENABLE_SPECTRUM=OFF 时不编频谱控件，kissfft 完全退出构建
# - EXWIDGETS_MINIMAL=ON 时只编 qlitemd 用到的控件（expander/progress ring）
option(EXWIDGETS_ENABLE_SPECTRUM
    "Build the FFT spectrum widget (requires kissfft)" ON)
option(EXWIDGETS_MINIMAL
    "Build only the ExWidgets used by qlitemd" OFF)

if(EXWIDGETS_ENABLE_SPECTRUM AND NOT TARGET kissfft)
    if(NOT CMAKE_C_COMPILER_LOADED)
        enable_language(C)
    endif()
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/kissfft"
        "${CMAKE_CURRENT_BINARY_DIR}/kissfft"
    )
endif()

if(EXWIDGETS_MINIMAL)
    set(EXWIDGETS_SOURCES
        controls/exexpander.cpp
        controls/exexpander.h
        controls/exfonticon.cpp
        controls/exfonticon.h
        gauges/exprogressring.cpp
        gauges/exprogressring.h
    )
else()
    file(GLOB_RECURSE EXWIDGETS_SOURCES CONFIGURE_DEPENDS
        "*.cpp"
        "*.h"
    )
    if(NOT EXWIDGETS_ENABLE_SPECTRUM)
        list(REMOVE_ITEM EXWIDGETS_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/media/exspectrumwidget.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/media/exspectrumwidget.h")
    endif()
endif()

add_library(ExWidgets SHARED
    ${EXWIDGETS_SOURCES}
)

add_library(ExWidgets::ExWidgets ALIAS ExWidgets)

target_compile_definitions(ExWidgets PRIVATE EXWIDGETS_LIBRARY)

target_include_directories(ExWidgets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/navigation>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dialogs>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gauges>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/color>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/controls>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/media>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ExWidgets>
)

target_include_directories(ExWidgets PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../FluentUI3Colors
    ${CMAKE_CURRENT_SOURCE_DIR}/../fluentui3style
    $<$<BOOL:${EXWIDGETS_ENABLE_SPECTRUM}>:${CMAKE_CURRENT_SOURCE_DIR}/../3rd/kissfft>
)

target_link_libraries(ExWidgets PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)

if(EXWIDGETS_ENABLE_SPECTRUM)
    target_link_libraries(ExWidgets PRIVATE kissfft)
endif()

set_target_properties(ExWidgets PROPERTIES
    DEBUG_POSTFIX d
)
