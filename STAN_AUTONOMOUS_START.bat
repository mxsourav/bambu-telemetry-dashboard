@echo off
setlocal
cd /d "%~dp0"

echo =========================================================================
echo   STAN Studio - Autonomous Desk Pet and AI Cognitive Companion
echo =========================================================================
echo.

echo [1/3] Verifying Windows Mobile Hotspot...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\enable_hotspot.ps1" >nul 2>&1

echo [2/3] Registering Windows Auto-Start on Boot / Logon...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup_autostart.ps1" >nul 2>&1

echo [3/3] Launching autonomous companion daemon in background...
taskkill /f /im pythonw.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul
wscript.exe "%~dp0tools\start_sync_silent.vbs"

echo.
echo =========================================================================
echo   [SUCCESS] STAN Desk Pet is now running 100%% autonomously!
echo   - Runs silently in background on boot / logon.
echo   - Auto-sleeps when you leave your desk (after 5 minutes).
echo   - Auto-wakes when you return to your laptop.
echo   - Real-time beat analysis, 3D print sentinel, and AI thought stream active.
echo.
echo   You can talk to STAN anytime by double-clicking: TALK_TO_STAN.bat
echo =========================================================================
echo.
endlocal
