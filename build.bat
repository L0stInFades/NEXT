@echo off
setlocal
echo ========================================
echo NEXT Engine - Build Script
echo ========================================

REM Locate CMake
set "CMAKE_EXE="
where cmake >nul 2>&1
if %ERRORLEVEL%==0 set "CMAKE_EXE=cmake"
if "%CMAKE_EXE%"=="" if exist "E:\CMake\bin\cmake.exe" set "CMAKE_EXE=E:\CMake\bin\cmake.exe"
if "%CMAKE_EXE%"=="" (
    echo ERROR: CMake not found in PATH. Please install CMake or set CMAKE_EXE.
    exit /b 1
)
echo Using CMake: %CMAKE_EXE%

REM Optional: override generator by setting CMAKE_GENERATOR env var
set "GENERATOR_ARG="
if not "%CMAKE_GENERATOR%"=="" set "GENERATOR_ARG=-G \"%CMAKE_GENERATOR%\""

REM Create build directory if not exists
if not exist build mkdir build

echo.
echo [1/2] Configuring project...
%CMAKE_EXE% -S . -B build -A x64 %GENERATOR_ARG%
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    exit /b 1
)

echo.
echo [2/2] Building project...
%CMAKE_EXE% --build build --config Debug
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    exit /b 1
)

echo.
echo ========================================
echo Build successful!
echo Executable: build\bin\Debug\song_demo.exe
echo ========================================
echo.
echo Run the demo:
echo   build\bin\Debug\song_demo.exe
echo.
endlocal
