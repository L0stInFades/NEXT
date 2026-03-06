@echo off
echo ========================================
echo NEXT Engine - Build with MSBuild
echo ========================================

REM Check MSBuild availability
if exist "E:\VS\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=E:\VS\MSBuild\Current\Bin\MSBuild.exe"
) else (
    echo ERROR: MSBuild not found!
    echo MSBuild location: E:\VS\MSBuild\Current\Bin\MSBuild.exe
    pause
    exit /b 1
)

echo Found MSBuild at: %MSBUILD%

REM Set up VC environment
call "E:\VS\VC\Auxiliary\Build\vcvars64.bat"

REM Create solution file (we need CMake for this)
echo.
echo [1/2] Checking if build directory exists...

if not exist build (
    echo Build directory not found. Creating...
    mkdir build

    REM Check if CMake is available
    where cmake >nul 2>nul
    if %ERRORLEVEL% NEQ 0 (
        echo.
        echo WARNING: CMake not found in PATH!
        echo.
        echo You have two options:
        echo.
        echo Option 1: Install CMake (recommended)
        echo   - Download from: https://cmake.org/download/
        echo   - Install with "Add CMake to system PATH" checked
        echo.
        echo Option 2: Use Visual Studio to generate solution
        echo   1. Open Visual Studio
        echo   2. File ^> Open ^> CMake...
        echo   3. Select E:\NEXT\CMakeLists.txt
        echo   4. Build ^> Build All
        echo.
        pause
        exit /b 1
    )

    cd build
    echo Generating solution with CMake...
    cmake .. -G "Visual Studio 17 2022" -A x64
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: CMake configuration failed
        pause
        exit /b 1
    )
    cd ..
) else (
    echo Build directory exists.
)

echo.
echo [2/2] Building solution...

cd build
"%MSBUILD%" NEXT.sln /p:Configuration=Debug /m /v:minimal

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Build successful!
    echo Executable: bin\Debug\song_demo.exe
    echo ========================================
) else (
    echo.
    echo Build failed. Check the error messages above.
)

cd ..

pause
