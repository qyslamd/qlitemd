# Minimal gtest test helper for qlitemd.

if(WIN32)
    get_filename_component(_qlitemd_qt_prefix
        "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
    set(QT_BIN_DIR "${_qlitemd_qt_prefix}/bin")
    set(QT_PLUGINS_DIR "${_qlitemd_qt_prefix}/plugins")
    set(TEST_ENV_QT
        "PATH=${QT_BIN_DIR};${PROJECT_BINARY_DIR}/bin;$ENV{PATH}"
        "QT_PLUGIN_PATH=${QT_PLUGINS_DIR}")
else()
    set(TEST_ENV_QT "")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin/tests)
set(TEST_DEFAULT_TIMEOUT 10 CACHE STRING "Default test timeout in seconds")

macro(add_unit_test)
    cmake_parse_arguments(ETEST "NO_RUN_SCRIPT;NO_GTEST_MAIN" "NAME"
        "SOURCES;LIBS;INCLUDES;QT_MODULES;LABELS" ${ARGN})

    add_executable(${ETEST_NAME} ${ETEST_SOURCES})
    set_target_properties(${ETEST_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)

    if(ETEST_QT_MODULES)
        set_target_properties(${ETEST_NAME} PROPERTIES AUTOMOC ON)
        list(TRANSFORM ETEST_QT_MODULES PREPEND "Qt${QT_VERSION_MAJOR}::")
        list(APPEND ETEST_LIBS ${ETEST_QT_MODULES})
    endif()

    if(ETEST_INCLUDES)
        target_include_directories(${ETEST_NAME} PRIVATE ${ETEST_INCLUDES})
    endif()

    if(ETEST_NO_GTEST_MAIN)
        set(_TEST_GTEST_LIBS gtest gmock)
    else()
        set(_TEST_GTEST_LIBS gtest gtest_main gmock)
    endif()
    target_link_libraries(${ETEST_NAME} PRIVATE
        ${_TEST_GTEST_LIBS} ${ETEST_LIBS})

    if(MSVC)
        target_compile_options(${ETEST_NAME} PRIVATE /W4 /WX- /EHsc /utf-8)
        target_compile_definitions(${ETEST_NAME} PRIVATE
            UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    endif()

    add_test(NAME ${ETEST_NAME} COMMAND ${ETEST_NAME}
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
    set_tests_properties(${ETEST_NAME} PROPERTIES
        TIMEOUT ${TEST_DEFAULT_TIMEOUT}
        LABELS "${ETEST_LABELS}"
        ENVIRONMENT "${TEST_ENV_QT}")
endmacro()
