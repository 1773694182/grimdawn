@echo off
setlocal

echo Checking GrimDawnTeleporter.exe processes...
tasklist /FI "IMAGENAME eq GrimDawnTeleporter.exe" | find /I "GrimDawnTeleporter.exe" >nul
if errorlevel 1 (
    echo No GrimDawnTeleporter.exe process is running.
    exit /b 0
)

echo Stopping GrimDawnTeleporter.exe...
taskkill /F /IM GrimDawnTeleporter.exe
if errorlevel 1 (
    echo Failed to stop GrimDawnTeleporter.exe.
    exit /b 1
)

echo GrimDawnTeleporter.exe has been stopped.
