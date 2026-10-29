set(TARGET_NAME "md_viewer")

set(ALL_SRCS
    main.cpp
    bookmarks.cpp
    bookmarks.h
    document_index.cpp
    document_index.h
    documents_panel.cpp
    documents_panel.h
    image_lightbox.cpp
    image_lightbox.h
    md_viewer_settings.cpp
    md_viewer_settings.h
    md_viewer_window.cpp
    md_viewer_window.h
    navigation_history.cpp
    navigation_history.h
    quick_open.cpp
    quick_open.h
    reader_appearance.cpp
    reader_appearance.h
    reading_progress.cpp
    reading_progress.h
    render_settings_panel.cpp
    text_search.cpp
    text_search.h
    render_settings_panel.h
    render_status_panel.cpp
    render_status_panel.h
    welcome_view.cpp
    welcome_view.h
    md_viewer.qrc
)

add_executable(${TARGET_NAME} WIN32 ${ALL_SRCS})

target_link_libraries(${TARGET_NAME} PRIVATE
    qlitemd::markdown_view
    ${APP_TARGET_PREFIX}_single_instance
    FluentUI3Style
    Frameless
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
if(QLITEMD_FORMULA_BACKEND STREQUAL "ratex")
    set(_md_viewer_formula_label "RaTeX")
else()
    set(_md_viewer_formula_label "QuickJS")
endif()
if(TARGET qlitemd::merman)
    set(_md_viewer_mermaid_label "mermaid:auto")
else()
    set(_md_viewer_mermaid_label "QuickJS")
endif()
if(QLITEMD_SVG_BACKEND STREQUAL "resvg")
    set(_md_viewer_svg_label "resvg")
else()
    set(_md_viewer_svg_label "QtSvg")
endif()
target_compile_definitions(${TARGET_NAME} PRIVATE
    APP_UI_FLUENT
    UNICODE
    _UNICODE
    QLITEMD_BACKEND_LABEL="${_md_viewer_formula_label} / ${_md_viewer_mermaid_label} / ${_md_viewer_svg_label}")

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE psapi)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(QLITEMD_INSTALL)
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
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX- /EHsc /utf-8)
endif()

if(QLITEMD_IS_TOP_LEVEL)
    configure_run_script("run_${TARGET_NAME}")
endif()

