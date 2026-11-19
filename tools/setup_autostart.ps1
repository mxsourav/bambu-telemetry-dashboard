$WshShell = New-Object -ComObject WScript.Shell
$StartupFolder = [System.Environment]::GetFolderPath('Startup')
$ShortcutPath = Join-Path $StartupFolder "DeskStationSpotifySync.lnk"
$Shortcut = $WshShell.CreateShortcut($ShortcutPath)
$Shortcut.TargetPath = "wscript.exe"
$Shortcut.Arguments = """d:\Gaar Fata Project\3d Printer Monitor\tools\start_sync_silent.vbs"""
$Shortcut.WorkingDirectory = "d:\Gaar Fata Project\3d Printer Monitor\tools"
$Shortcut.Description = "Desk Station Spotify & Media Sync (Silent Background)"
$Shortcut.Save()
Write-Host "Auto-start shortcut successfully installed in Windows Startup folder: $ShortcutPath"
