@echo off
setlocal
echo Stopping Desk Station background sync...
powershell -NoProfile -Command "Get-Process pythonw -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -like '*spotify_desk_sync*' -or $_.CommandLine -like '*start_sync*' } | Stop-Process -Force"
echo Stopped.
endlocal
