@echo off
setlocal
cd /d "%~dp0"
echo Starting Desk Station autonomous background sync...
wscript "tools\start_sync_silent.vbs"
timeout /t 2 /nobreak >nul
echo Done. Autonomous sync is running in the background.
endlocal
