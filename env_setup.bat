@echo off
REM Developer Command Prompt Launcher for NEXT Engine
REM This script helps set up the build environment without requiring CMake in PATH

echo ========================================
echo NEXT Engine - Build Environment Setup
echo ========================================

REM Try to find Visual Studio installation
set "VS2022="
set "VS2019="

REM Check for VS2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS2022=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS2022=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS2022=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

REM Check for VS2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS2019=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS2019=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS2019=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

REM Use the latest available
if defined VS2022 (
    echo Found Visual Studio 2022
    call "%VS2022%"
) else if defined VS2019 (
    echo Found Visual Studio 2019
    call "%VS2019%"
) else (
    echo ERROR: Visual Studio not found!
    echo Please install Visual Studio 2019/2022 with "Desktop development with C++" workload
    pause
    exit /b 1
)

echo.
echo Build environment ready!
echo Now you can run:
echo   - build_msvc.bat  (build without CMake)
echo   - build.bat       (build with CMake, if installed)
echo.
cmd /k
