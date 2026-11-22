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
if exist "%ProgramFiles%\BraveSoftware\Brave-Browser\Application\brave.exe" (
    start "" "%ProgramFiles%\BraveSoftware\Brave-Browser\Application\brave.exe" --unsafely-treat-insecure-origin-as-secure="http://192.168.137.35" "https://web-tau-five-60.vercel.app/"
) else if exist "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" (
    start "" "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" --unsafely-treat-insecure-origin-as-secure="http://192.168.137.35" "https://web-tau-five-60.vercel.app/"
) else (
    start "" "https://web-tau-five-60.vercel.app/"
)

echo Done! You can now use your Vercel deployment with zero security blocks.
timeout /t 5
