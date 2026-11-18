@echo off
title STAN studio. Spotify Permanent Pairing
cd /d "%~dp0"
echo =========================================================
echo   STAN studio. - Link Spotify Permanently to Desk Monitor
echo =========================================================
python "%~dp0tools\spotify_auth.py"
pause
