@echo off
setlocal
chcp 65001 >nul

rem 兼容性脚本：恢复被 disable-plugin.bat 改名的插件 DLL。

set "SCRIPT_DIR=%~dp0"
set "FOUND=0"

call :Enable "%SCRIPT_DIR%GrimDawnTeleporter.Plugin.dll"
call :Enable "%SCRIPT_DIR%dist\GrimDawnTeleporter-x64\GrimDawnTeleporter.Plugin.dll"
call :Enable "%SCRIPT_DIR%src\GrimDawnTeleporter\bin\x64\Release\net8.0-windows\win-x64\GrimDawnTeleporter.Plugin.dll"
rem 常见的手工部署目录（可根据需要自行增删）
call :Enable "%USERPROFILE%\Desktop\win-x64\GrimDawnTeleporter.Plugin.dll"

if "%FOUND%"=="0" (
    echo 未找到已被禁用的插件（*.dll.off），无需处理。
) else (
    echo.
    echo 已恢复插件。重新打开工具并点击“附加插件”即可注入。
)

echo.
pause
exit /b 0

:Enable
if exist "%~1.off" (
    if exist "%~1" del /f /q "%~1" >nul 2>&1
    move /y "%~1.off" "%~1" >nul 2>&1
    if errorlevel 1 (
        echo [失败] 无法恢复，请确认文件未被占用：%~1.off
    ) else (
        echo [成功] 已恢复：%~1.off -^> %~1
        set "FOUND=1"
    )
)
exit /b 0
