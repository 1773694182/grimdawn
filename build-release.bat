@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ARGS=%*"

if "%ARGS%"=="" set "ARGS=-Configuration Release"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-release.ps1" %ARGS%
if errorlevel 1 (
    echo.
    echo Build failed.
    echo 提示：脚本会打印缺失的工具与安装命令；也可以先运行 build-launcher.bat 只重建启动器。
    pause
    exit /b 1
)

echo.
echo Build succeeded.
pause
