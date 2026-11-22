@echo off
title STAN Studio - Auto Setup Vercel Cloud Deployment
cd /d "%~dp0"

echo ======================================================================
echo   STAN Studio - Auto Setup Vercel Cloud Deployment
echo   Enabling automatic browser access for https://*.vercel.app
echo ======================================================================
echo.

where python >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Error: Python is not installed or not in PATH.
    pause
    exit /b 1
)

python tools\setup_vercel_pna.py

echo.
echo Launching browser with bypass flags...
start msedge.exe --unsafely-treat-insecure-origin-as-secure="http://192.168.137.35" "https://bambu-telemetry-dashboard.vercel.app/" 2>nul || start chrome.exe --unsafely-treat-insecure-origin-as-secure="http://192.168.137.35" "https://bambu-telemetry-dashboard.vercel.app/" 2>nul

echo Done! You can now use your Vercel deployment with zero security blocks.
timeout /t 5
