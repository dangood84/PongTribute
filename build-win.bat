@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake is not on PATH.
  echo Install CMake, or Visual Studio with the "Desktop development with C++" workload.
  exit /b 1
)

echo Configuring build\
cmake -S . -B build
if errorlevel 1 (
  echo Configure failed. If Raylib is not installed, Git must be on PATH so CMake can download it.
  exit /b 1
)

echo.
echo Compiling
cmake --build build --config Release
if errorlevel 1 (
  echo Compile failed.
  exit /b 1
)

echo.
echo Run:  build\pong.exe
