@echo off
setlocal

rem 只重建 .NET 启动器（不编译 C++ 插件），需要 .NET 8 SDK，但不需要 Visual Studio。
rem 用法：build-launcher.bat [Release|Debug]

set "SCRIPT_DIR=%~dp0"
set "ARGS=%*"

if "%ARGS%"=="" set "ARGS=Release"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-release.ps1" -SkipPlugin %ARGS%
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo Build succeeded.
pause
