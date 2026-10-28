# Generate a run script that prepares Qt's runtime environment before
# launching an application. Windows needs both PATH (Qt DLLs) and
# QT_PLUGIN_PATH (platform/style/image plugins); Linux normally resolves
# both through the system Qt installation.
#
# configure_run_script(<output_basename> [ARGS "<default args>"])
#   Output goes to CMAKE_RUNTIME_OUTPUT_DIRECTORY, matching the executable.
#   The caller's TARGET_NAME is substituted into the template.
macro(configure_run_script output_basename)
    cmake_parse_arguments(CRS "" "ARGS" "" ${ARGN})

    get_filename_component(_qlitemd_qt_prefix
        "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
    set(QT_BIN_DIR "${_qlitemd_qt_prefix}/bin")
    set(QT_PLUGINS_DIR "${_qlitemd_qt_prefix}/plugins")

    get_target_property(_run_output_dir ${TARGET_NAME} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _run_output_dir)
        set(_run_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    if(NOT _run_output_dir)
        set(_run_output_dir "${PROJECT_BINARY_DIR}/bin")
    endif()

    if(WIN32)
        set(_template "${PROJECT_SOURCE_DIR}/cmake/template/run_app.bat.in")
        set(_output "${_run_output_dir}/${output_basename}.bat")
        file(TO_NATIVE_PATH "${_run_output_dir}" RUN_DIR)
    elseif(UNIX)
        set(_template "${PROJECT_SOURCE_DIR}/cmake/template/run_app.sh.in")
        set(_output "${_run_output_dir}/${output_basename}.sh")
        set(RUN_DIR "${_run_output_dir}")
    endif()

    if(_template)
        set(PROGRAM_ARGS "${CRS_ARGS}")
        configure_file("${_template}" "${_output}" @ONLY)
        if(UNIX)
            file(CHMOD "${_output}"
                PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                            GROUP_READ GROUP_EXECUTE
                            WORLD_READ WORLD_EXECUTE)
        endif()
    endif()

    unset(PROGRAM_ARGS)
    unset(RUN_DIR)
    unset(_template)
    unset(_output)
    unset(_run_output_dir)
    unset(_qlitemd_qt_prefix)
endmacro()
