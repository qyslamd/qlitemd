set(TARGET_NAME "md_viewer")

set(ALL_SRCS
    main.cpp
    md_viewer_settings.cpp
    md_viewer_settings.h
    md_viewer_window.cpp
    md_viewer_window.h
    render_settings_panel.cpp
    render_settings_panel.h
    render_status_panel.cpp
    render_status_panel.h
    md_viewer.qrc
)

add_executable(${TARGET_NAME} WIN32 ${ALL_SRCS})

target_link_libraries(${TARGET_NAME} PRIVATE
    ${APP_TARGET_PREFIX}_markdown
    ${APP_TARGET_PREFIX}_single_instance
    FluentUI3Style
    Frameless
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
target_compile_definitions(${TARGET_NAME} PRIVATE
    APP_UI_FLUENT
    UNICODE
    _UNICODE)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE psapi)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS ${TARGET_NAME}
    RUNTIME DESTINATION bin
    COMPONENT Runtime)

if(WIN32)
    # FluentUI3Style/ExWidgets are built in-tree and are not Qt plugins.
    install(TARGETS FluentUI3Style ExWidgets
        RUNTIME DESTINATION bin COMPONENT Runtime
        LIBRARY DESTINATION lib COMPONENT Runtime)
    get_filename_component(_md_viewer_qt_prefix
        "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
    find_program(QLITEMD_WINDEPLOYQT
        NAMES windeployqt
        HINTS "${_md_viewer_qt_prefix}/bin")
    if(QLITEMD_WINDEPLOYQT)
        install(CODE
            "execute_process(COMMAND \"${QLITEMD_WINDEPLOYQT}\" --dir \"\${CMAKE_INSTALL_PREFIX}/bin\" \"\${CMAKE_INSTALL_PREFIX}/bin/md_viewer.exe\")"
            COMPONENT Runtime)
    endif()
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX- /EHsc /utf-8)
endif()

configure_run_script("run_${TARGET_NAME}")

