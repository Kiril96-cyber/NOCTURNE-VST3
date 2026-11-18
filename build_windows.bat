@echo off
setlocal
where cmake >nul 2>nul || (echo CMake is not installed or not on PATH.& exit /b 1)
cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo Build finished. Look in build\NOCTURNE_artefacts\Release\VST3\
pause
