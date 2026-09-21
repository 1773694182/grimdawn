@echo off
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

echo Checking GrimDawnTeleporter.exe processes...
tasklist /FI "IMAGENAME eq GrimDawnTeleporter.exe" | find /I "GrimDawnTeleporter.exe" >nul
if errorlevel 1 (
    echo No GrimDawnTeleporter.exe process is running.
    pause
    exit /b 0
)

echo Stopping GrimDawnTeleporter.exe...
taskkill /F /IM GrimDawnTeleporter.exe
if errorlevel 1 (
    echo Failed to stop GrimDawnTeleporter.exe.
    pause
    exit /b 1
)

echo GrimDawnTeleporter.exe has been stopped.
pause
