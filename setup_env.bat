@echo off
echo ========================================
echo NEXT Engine - Setup Build Environment
echo ========================================

echo Setting up Visual Studio environment...
call "E:\VS\VC\Auxiliary\Build\vcvars64.bat"

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to set up Visual Studio environment
    echo VS location: E:\VS
    pause
    exit /b 1
)

echo.
echo Visual Studio environment ready!
echo.
echo Available build commands:
echo.
echo   If you have CMake installed:
echo     build.bat              - Build using CMake (recommended)
echo.
echo   Without CMake (option 1):
echo     1. Open Visual Studio
echo     2. File ^> Open ^> CMake...
echo     3. Select E:\NEXT\CMakeLists.txt
echo     4. Build ^> Build All
echo.
echo   Without CMake (option 2):
echo     Install CMake: https://cmake.org/download/
echo.
echo Current tools available:
where msbuild
where cmake
echo.
echo Press any key to open command prompt...
pause >nul
cmd /k
