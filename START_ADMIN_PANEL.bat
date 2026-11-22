@echo off
title STAN studio - Desk Station Admin Panel
cls
echo =====================================================================
echo  STAN studio. Local Desk Station Admin Panel (VisionOS Dark Mode)
echo =====================================================================
echo  Starting local admin server on http://localhost:3000 ...
echo  Direct ESP32 IP: http://192.168.137.35/
echo =====================================================================
python "%~dp0tools\admin_server.py"
pause
