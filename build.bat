@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================
:: qlitemd build helper
::
::   build.bat [-a x86|x64] [-t debug|release|relwithdebinfo]
::             [--tests on|off | --no-tests] [-j N]
::
::   x86 -> VS2019 + Qt 5.15.2 msvc2019
::   x64 -> VS2022 + Qt 6.8.3 msvc2022_64
::
:: Override the kits with:
::   QLITEMD_QT5_X86  (default D:\Qt22\5.15.2\msvc2019)
::   QLITEMD_QT6_X64  (default D:\Qt\6.8.3\msvc2022_64)
:: ============================================================

chcp 65001 >nul
set "ROOT=%~dp0"

set "ARCH="
set "CONFIG="
set "TESTS="
set "JOBS="

:parse_flags
if "%~1"=="" goto :apply_defaults
if /i "%~1"=="-h" goto :help
if /i "%~1"=="--help" goto :help

if /i "%~1"=="-a" (
    if "%~2"=="" goto :missing_arch
    set "ARCH=%~2"
    shift
    shift
    goto :parse_flags
)
if /i "%~1"=="--arch" (
    if "%~2"=="" goto :missing_arch
    set "ARCH=%~2"
    shift
    shift
    goto :parse_flags
)

if /i "%~1"=="-t" (
    if "%~2"=="" goto :missing_type
    set "CONFIG=%~2"
    shift
    shift
    goto :parse_flags
)
if /i "%~1"=="--type" (
    if "%~2"=="" goto :missing_type
    set "CONFIG=%~2"
    shift
    shift
    goto :parse_flags
)

if /i "%~1"=="-j" (
    if "%~2"=="" goto :missing_jobs
    set "JOBS=%~2"
    shift
    shift
    goto :parse_flags
)
if /i "%~1"=="--jobs" (
    if "%~2"=="" goto :missing_jobs
    set "JOBS=%~2"
    shift
    shift
    goto :parse_flags
)

if /i "%~1"=="--tests" (
    if "%~2"=="" goto :missing_tests
    set "TESTS=%~2"
    shift
    shift
    goto :parse_flags
)
if /i "%~1"=="--no-tests" (
    set "TESTS=off"
    shift
    goto :parse_flags
)

set "ARG=%~1"
if /i "!ARG:~0,7!"=="--arch=" (
    set "ARCH=!ARG:~7!"
    shift
    goto :parse_flags
)
if /i "!ARG:~0,7!"=="--type=" (
    set "CONFIG=!ARG:~7!"
    shift
    goto :parse_flags
)
if /i "!ARG:~0,7!"=="--jobs=" (
    set "JOBS=!ARG:~7!"
    shift
    goto :parse_flags
)
if /i "!ARG:~0,8!"=="--tests=" (
    set "TESTS=!ARG:~8!"
    shift
    goto :parse_flags
)

echo [ERROR] unknown argument: %~1
goto :usage

:apply_defaults
if not defined ARCH set "ARCH=x86"
if not defined CONFIG set "CONFIG=relwithdebinfo"
if not defined TESTS set "TESTS=off"

:: ------------------------------------------------------ validation
if /i not "%ARCH%"=="x86" if /i not "%ARCH%"=="x64" (
    echo [ERROR] unknown arch: %ARCH% ^(expected x86 or x64^)
    goto :usage
)

if /i "%CONFIG%"=="relwithdebinfo" (
    set "BUILD_TYPE=RelWithDebInfo"
    set "CFG_LABEL="
) else if /i "%CONFIG%"=="release" (
    set "BUILD_TYPE=Release"
    set "CFG_LABEL=release"
) else if /i "%CONFIG%"=="debug" (
    set "BUILD_TYPE=Debug"
    set "CFG_LABEL=debug"
) else (
    echo [ERROR] unknown type: %CONFIG% ^(expected relwithdebinfo, debug or release^)
    goto :usage
)

if /i "%TESTS%"=="on" (
    set "TESTS_VALUE=ON"
) else if /i "%TESTS%"=="off" (
    set "TESTS_VALUE=OFF"
) else (
    echo [ERROR] unknown tests value: %TESTS% ^(expected on or off^)
    goto :usage
)

if defined JOBS (
    for /f "delims=0123456789" %%A in ("!JOBS!") do goto :invalid_jobs
    if "!JOBS:~0,1!"=="0" goto :invalid_jobs
)

:: ------------------------------------------------------ kit selection
if /i "%ARCH%"=="x86" (
    set "QT_MAJOR=5"
    set "QT_LABEL=qt5_15_2"
    set "QT_DIR=%QLITEMD_QT5_X86%"
    if not defined QT_DIR set "QT_DIR=D:\Qt22\5.15.2\msvc2019"
) else (
    set "QT_MAJOR=6"
    set "QT_LABEL=qt6_8_3"
    set "QT_DIR=%QLITEMD_QT6_X64%"
    if not defined QT_DIR set "QT_DIR=D:\Qt\6.8.3\msvc2022_64"
)

if defined CFG_LABEL (
    set "BUILD_DIR=%ROOT%build\%QT_LABEL%-%ARCH%-%CFG_LABEL%"
) else (
    set "BUILD_DIR=%ROOT%build\%QT_LABEL%-%ARCH%"
)

:: ------------------------------------------------------ Visual Studio env
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found: "%VSWHERE%"
    exit /b 2
)

set "VS_MARKER=2019"
if /i "%ARCH%"=="x64" set "VS_MARKER=2022"

set "VS_DIR="
for /f "usebackq delims=" %%I in (`call "%VSWHERE%" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath ^| findstr /i /c:"%VS_MARKER%"`) do (
    if not defined VS_DIR set "VS_DIR=%%I"
)
if not defined VS_DIR (
    echo [ERROR] no Visual Studio %VS_MARKER% installation with C++ tools found.
    echo         Install the C++ workload or adjust the VS lookup in build.bat.
    exit /b 2
)

echo ============================================================
echo   Arch    : %ARCH%
echo   Config  : %BUILD_TYPE%
echo   Tests   : %TESTS_VALUE%
echo   VS      : %VS_DIR%
echo   Qt      : %QT_DIR%
echo   Build   : %BUILD_DIR%
echo   Jobs    : %JOBS%
echo ============================================================

:: vcvarsall and CMake/Qt helpers sometimes call vswhere by bare name.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VS_DIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 (
    echo [ERROR] failed to initialize the Visual Studio environment.
    exit /b 3
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH.
    exit /b 3
)
where ninja >nul 2>&1
if errorlevel 1 (
    echo [ERROR] ninja not found in PATH.
    exit /b 3
)
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [ERROR] Qt kit not found: "%QT_DIR%"
    exit /b 3
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: ------------------------------------------------------------ configure
cmake -S "%ROOT%." -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    "-DCMAKE_PREFIX_PATH=%QT_DIR%" ^
    -DQLITEMD_QT_MAJOR=%QT_MAJOR% ^
    -DQLITEMD_BUILD_TESTS=%TESTS_VALUE% ^
    -DQLITEMD_BUILD_QT_MD_VIEWER=ON ^
    -DQLITEMD_BUILD_QT_MD_OBSERVATORY=ON ^
    -DQLITEMD_SVG_BACKEND=resvg
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 4
)

:: ---------------------------------------------------------------- build
if defined JOBS (
    cmake --build "%BUILD_DIR%" --parallel %JOBS%
) else (
    cmake --build "%BUILD_DIR%"
)
if errorlevel 1 (
    echo [ERROR] build failed.
    exit /b 5
)

:: ---------------------------------------------------------------- tests
if /i not "%TESTS%"=="on" goto :after_tests
set "PATH=%QT_DIR%\bin;%BUILD_DIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"
ctest --test-dir "%BUILD_DIR%" --output-on-failure -L MARKDOWN
if errorlevel 1 (
    echo [ERROR] tests failed.
    exit /b 6
)
:after_tests

echo.
echo [SUCCESS] %BUILD_DIR%
endlocal
exit /b 0

:missing_arch
echo [ERROR] missing value for -a/--arch
goto :usage
:missing_type
echo [ERROR] missing value for -t/--type
goto :usage
:missing_jobs
echo [ERROR] missing value for -j/--jobs
goto :usage
:missing_tests
echo [ERROR] missing value for --tests
goto :usage
:invalid_jobs
echo [ERROR] jobs must be a positive integer: %JOBS%
goto :usage

:help
call :print_usage
exit /b 0

:usage
call :print_usage
exit /b 1

:print_usage
echo Usage: build.bat [-a arch] [-t type] [--tests on^|off] [-j N]
echo.
echo Options:
echo   -a, --arch ^<x86^|x64^>      x86 (default) = VS2019 + Qt 5.15.2
echo                             x64           = VS2022 + Qt 6.8.3
echo   -t, --type ^<type^>         relwithdebinfo (default), debug or release
echo       --tests ^<on^|off^>      run MARKDOWN tests after build (default off)
echo       --no-tests            same as --tests off
echo   -j, --jobs ^<N^>            build parallelism; omit for CMake's default
echo   -h, --help                show this help
echo.
echo Defaults: -a x86 -t relwithdebinfo --tests off (Qt 5.15.2);
echo           omit -j to let CMake choose the parallelism.
echo.
echo Examples (flag order does not matter):
echo   build.bat
echo   build.bat -a x86 -t debug
echo   build.bat -t release --no-tests
echo   build.bat -a x64 -j 4 -t relwithdebinfo
exit /b 0
