# NEXT Engine - Build Instructions

## Prerequisites

- Visual Studio 2022 (or 2019) with C++ development tools
- CMake 3.20 or later
- Windows 10/11

## Build Steps

### 1. Install CMake

If you don't have CMake installed:
- Download from https://cmake.org/download/
- Add CMake to PATH during installation

### 2. Generate Solution

```cmd
cd E:\NEXT
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
```

### 3. Build

Using CMake:
```cmd
cmake --build . --config Debug
```

Or open the generated solution file in Visual Studio:
```
E:\NEXT\build\NEXT.sln
```

## Quick Build Script

If CMake is installed and added to PATH, run:
```cmd
build.bat
```

## Output

The executable will be in:
- `E:\NEXT\build\bin\Debug\song_demo.exe`

## Verification

Run `song_demo.exe` to verify:
- Window opens
- Can press WASD to log movement
- Press ESC to exit
- Runs for 5 minutes without crashes
