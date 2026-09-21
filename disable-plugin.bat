@echo off
setlocal
chcp 65001 >nul

rem 兼容性脚本：把插件 DLL 改名，使工具无法注入插件。
rem 适用于无法重新编译插件、且“注入后游戏闪退”的机器（例如虚拟机 + 旧版 VC 运行库）。

set "SCRIPT_DIR=%~dp0"
set "FOUND=0"

call :Disable "%SCRIPT_DIR%GrimDawnTeleporter.Plugin.dll"
call :Disable "%SCRIPT_DIR%dist\GrimDawnTeleporter-x64\GrimDawnTeleporter.Plugin.dll"
call :Disable "%SCRIPT_DIR%src\GrimDawnTeleporter\bin\x64\Release\net8.0-windows\win-x64\GrimDawnTeleporter.Plugin.dll"
rem 常见的手工部署目录（可根据需要自行增删）
call :Disable "%USERPROFILE%\Desktop\win-x64\GrimDawnTeleporter.Plugin.dll"

if "%FOUND%"=="0" (
    echo 未找到 GrimDawnTeleporter.Plugin.dll，无需处理。
) else (
    echo.
    echo 已禁用插件。工具将以“外部内存兼容模式”运行，传送功能仍可使用。
    echo 如需恢复，请运行 enable-plugin.bat。
)

echo.
pause
exit /b 0

:Disable
if exist "%~1" (
    if exist "%~1.off" del /f /q "%~1.off" >nul 2>&1
    move /y "%~1" "%~1.off" >nul 2>&1
    if errorlevel 1 (
        echo [失败] 插件正在被游戏/工具占用，请先关闭游戏与工具后重试：%~1
    ) else (
        echo [成功] 已重命名：%~1 -^> %~1.off
        set "FOUND=1"
    )
)
exit /b 0
