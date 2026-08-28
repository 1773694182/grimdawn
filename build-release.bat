@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "CONFIGURATION=%~1"

if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-release.ps1" -Configuration "%CONFIGURATION%"
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo Build succeeded.
pause
