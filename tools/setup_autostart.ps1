$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$WshShell = New-Object -ComObject WScript.Shell
$StartupFolder = [System.Environment]::GetFolderPath('Startup')
$ShortcutPath = Join-Path $StartupFolder "DeskStationSpotifySync.lnk"
$Shortcut = $WshShell.CreateShortcut($ShortcutPath)
$Shortcut.TargetPath = "wscript.exe"
$Shortcut.Arguments = """" + (Join-Path $ScriptDir "start_sync_silent.vbs") + """"
$Shortcut.WorkingDirectory = $ProjectDir
$Shortcut.Description = "Desk Station Spotify and Media Sync (Silent Background)"
$Shortcut.Save()
Write-Host "Auto-start shortcut successfully installed in Windows Startup folder: $ShortcutPath"
