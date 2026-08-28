@echo off
REM ---------------------------------------------------------------------------
REM  Builds GlbSource.dll and drops it into Resolume's Extra Effects folder.
REM  Needs: Visual Studio 2019/2022 with the C++ desktop workload, CMake, Git.
REM  Run it from anywhere, it works out its own location.
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [X] CMake is not on PATH. Install it from https://cmake.org/download/
    echo     or use the "Developer Command Prompt for VS".
    exit /b 1
)

where git >nul 2>nul
if errorlevel 1 (
    echo [X] Git is not on PATH. It is needed to fetch the Resolume FFGL SDK.
    echo     Install Git, or pass -DFFGL_SDK_DIR=path\to\ffgl to cmake yourself.
    exit /b 1
)

echo.
echo === Configuring ===
cmake -B build -A x64
if errorlevel 1 goto :failed

echo.
echo === Building (Release) ===
cmake --build build --config Release
if errorlevel 1 goto :failed

echo.
echo === Installing into Resolume ===
cmake --build build --config Release --target install-plugin
if errorlevel 1 goto :failed

echo.
echo Done. Restart Resolume Arena, then look for "GLB Model Source"
echo in the Sources tab of the Effects panel.
exit /b 0

:failed
echo.
echo [X] Build failed. Scroll up for the first error.
exit /b 1
