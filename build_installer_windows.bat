@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo       NOCTURNE VST3 - BUILD + INSTALLER
echo ==========================================

where cmake >nul 2>nul
if errorlevel 1 (
  echo ERROR: CMake 3.22+ is not installed or not in PATH.
  pause
  exit /b 1
)

where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: Visual Studio C++ Build Tools are not available.
  echo Run this file from "x64 Native Tools Command Prompt for VS 2022".
  pause
  exit /b 1
)

cmake -S . -B build -A x64
if errorlevel 1 goto fail
cmake --build build --config Release
if errorlevel 1 goto fail

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo.
  echo VST3 build succeeded, but Inno Setup 6 was not found.
  echo Install Inno Setup 6, then run:
  echo "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" installer\NOCTURNE.iss
  pause
  exit /b 2
)

if not exist dist mkdir dist
"%ISCC%" installer\NOCTURNE.iss
if errorlevel 1 goto fail

echo.
echo SUCCESS: dist\NOCTURNE_Setup_x64.exe
echo Run the installer as administrator. It installs the VST3 to:
echo C:\Program Files\Common Files\VST3\NOCTURNE.vst3
pause
exit /b 0

:fail
echo.
echo BUILD FAILED. Read the error messages above.
pause
exit /b 1
