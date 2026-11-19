@echo off
echo Stopping Desk Sync...
powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*spotify_desk_sync.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
del /q "%~dp0.sync.lock" 2>nul
echo Done.
