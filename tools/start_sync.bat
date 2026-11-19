@echo off
set PYTHON_EXE=C:\Users\rodd\AppData\Local\Programs\Python\Python314\pythonw.exe
if not exist "%PYTHON_EXE%" set PYTHON_EXE=pythonw.exe

start "" "%PYTHON_EXE%" "%~dp0spotify_desk_sync.py"
echo [OK] Desk Station Spotify & Media Sync is running silently in the background!
pause
