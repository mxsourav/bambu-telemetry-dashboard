Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
ScriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
ProjectDir = fso.GetParentFolderName(ScriptDir)
WshShell.CurrentDirectory = ProjectDir
WshShell.Run "pythonw """ & fso.BuildPath(ScriptDir, "spotify_desk_sync.py") & """", 0, False
