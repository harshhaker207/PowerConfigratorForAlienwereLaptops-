@echo off
REM Build script for PowerConfigurator
REM Requires: Visual Studio 2022 with C++ workload, CMake 3.20+

set BUILD_DIR=build
set ARCH=x64

REM Try Visual Studio 2022 first, fall back to Visual Studio 2019
set GENERATOR="Visual Studio 17 2022"
reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\SxS\VS7" /v 17.0 >nul 2>&1
if errorlevel 1 (
    set GENERATOR="Visual Studio 16 2019"
)

echo Building PowerConfigurator...
echo.

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd "%BUILD_DIR%"

cmake -G %GENERATOR% -A %ARCH% ..
if errorlevel 1 (
    echo CMake configure failed!
    exit /b 1
)

cmake --build . --config Release
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo.
echo Build successful!
echo Executable: %CD%\bin\PowerConfigurator.exe

cd ..