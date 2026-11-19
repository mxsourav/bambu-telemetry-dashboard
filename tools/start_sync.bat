@echo off
tasklist /fi "imagename eq pythonw.exe" 2>nul | find /i "pythonw.exe" >nul
if "%errorlevel%"=="0" (
    echo [INFO] Desk Station Sync is ALREADY running in the background!
) else (
    echo [STARTING] Launching Desk Station Media Sync in background...
    wscript.exe "%~dp0start_sync_silent.vbs"
    timeout /t 1 >nul
    echo [OK] Desk Station Media Sync is now running!
)
pause
