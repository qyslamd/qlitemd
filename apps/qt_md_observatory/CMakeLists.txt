set(TARGET_NAME "qt_md_observatory")

set(ALL_SRCS
    main.cpp
    app_theme.cpp
    app_theme.h
    command_palette.cpp
    command_palette.h
    document_rail.cpp
    document_rail.h
    instrument_math.cpp
    instrument_math.h
    observatory_window.cpp
    observatory_window.h
    title_bar.cpp
    title_bar.h
    workspace_model.cpp
    workspace_model.h
    workspace_panel.cpp
    workspace_panel.h
)

add_executable(${TARGET_NAME} WIN32 ${ALL_SRCS})

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME} PRIVATE
    qlitemd::markdown_view
    QWindowKit::Widgets
    QWindowKit::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
)

set_target_properties(${TARGET_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    OUTPUT_NAME qt_md_observatory
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /W4 /WX- /EHsc)
endif()
if(TARGET qlitemd_runtime_dlls)
    add_dependencies(${TARGET_NAME} qlitemd_runtime_dlls)
endif()

# UI source language is English; zh_CN is compiled next to the executable.
get_filename_component(_obs_qt_prefix "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
find_program(QLITEMD_OBS_LRELEASE NAMES lrelease HINTS "${_obs_qt_prefix}/bin")
if(QLITEMD_OBS_LRELEASE)
    set(_obs_ts "${CMAKE_CURRENT_SOURCE_DIR}/translations/qt_md_observatory_zh_CN.ts")
    set(_obs_qm "${CMAKE_CURRENT_BINARY_DIR}/qt_md_observatory_zh_CN.qm")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${TARGET_NAME}>/translations
        COMMAND ${QLITEMD_OBS_LRELEASE} ${_obs_ts} -qm ${_obs_qm}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_obs_qm}
                $<TARGET_FILE_DIR:${TARGET_NAME}>/translations/
        COMMENT "Compiling qt_md_observatory translations")
    if(QLITEMD_INSTALL)
        install(FILES ${_obs_qm}
            DESTINATION bin/translations COMPONENT Runtime)
    endif()
endif()

if(QLITEMD_INSTALL)
    install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION bin COMPONENT Runtime)
endif()


configure_run_script("run_${TARGET_NAME}")