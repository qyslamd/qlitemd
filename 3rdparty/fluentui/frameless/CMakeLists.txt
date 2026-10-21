if(CMAKE_VERSION VERSION_LESS "3.19")
    message(FATAL_ERROR
        "BUILD_FRAMELESS requires CMake 3.19 or newer (found ${CMAKE_VERSION})."
    )
endif()

if(NOT TARGET QWindowKit::Widgets)
    set(_frameless_qwindowkit_dir "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/qwindowkit")
    if(EXISTS "${_frameless_qwindowkit_dir}/CMakeLists.txt")
        set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "" FORCE)
        set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "" FORCE)
        set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF CACHE BOOL "" FORCE)
        set(QWINDOWKIT_INSTALL ON CACHE BOOL "" FORCE)
        add_subdirectory(
            "${_frameless_qwindowkit_dir}"
            "${CMAKE_CURRENT_BINARY_DIR}/qwindowkit"
        )
    else()
        find_package(QWindowKit CONFIG REQUIRED)
    endif()
endif()

if(NOT TARGET QWindowKit::Widgets)
    message(FATAL_ERROR "BUILD_FRAMELESS is ON, but QWindowKit::Widgets is unavailable.")
endif()

foreach(_qwk_target IN ITEMS QWKCore QWKWidgets)
    if(TARGET ${_qwk_target} AND CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set_target_properties(${_qwk_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
        )
        foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            if(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg})
                set_target_properties(${_qwk_target} PROPERTIES
                    RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg}}"
                    LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg}}"
                    ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg}}"
                )
            endif()
        endforeach()
    endif()
endforeach()

file(GLOB FRAMELESS_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "*.h"
)

add_library(Frameless STATIC
    ${FRAMELESS_SOURCES}
)

add_library(Frameless::Frameless ALIAS Frameless)

target_include_directories(Frameless PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Frameless>
)

target_link_libraries(Frameless PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    QWindowKit::Widgets
)

set_target_properties(Frameless PROPERTIES
    DEBUG_POSTFIX d
    EXPORT_NAME Frameless
    POSITION_INDEPENDENT_CODE ON
)
