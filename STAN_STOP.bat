@echo off
title Stop STAN Autonomous Companion
cd /d "%~dp0"

echo Stopping STAN Autonomous Desk Companion processes...
taskkill /f /im pythonw.exe >nul 2>&1
taskkill /f /fi "windowtitle eq STAN*" >nul 2>&1

echo [OK] All STAN background processes terminated.
timeout /t 2 >nul
