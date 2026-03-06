@echo off
REM Direct MSBuild build without CMake (alternative approach)

echo ========================================
echo Building NEXT Engine with MSBuild
echo ========================================

REM Check if we're in developer command prompt
where msbuild >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: MSBuild not found!
    echo Please run env_setup.bat first to set up the build environment
    pause
    exit /b 1
)

REM Note: This script is a placeholder
echo.
echo For now, please use CMake to generate the solution:
echo   1. Run env_setup.bat
echo   2. Run build.bat (requires CMake)
echo.
echo Or manually:
echo   cmake .. -G "Visual Studio 17 2022" -A x64
echo   cmake --build . --config Debug
echo.
pause
