$ErrorActionPreference = 'SilentlyContinue'
[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media, ContentType = WindowsRuntime] | Out-Null
$asyncOp = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()
while ($asyncOp.Status -eq 0) { Start-Sleep -Milliseconds 20 }
$mgr = $asyncOp.GetResults()
$session = $mgr.GetCurrentSession()
if ($session) {
    $propOp = $session.TryGetMediaPropertiesAsync()
    while ($propOp.Status -eq 0) { Start-Sleep -Milliseconds 20 }
    $props = $propOp.GetResults()
    
    $posMs = 0
    $durMs = 0
    $timeline = $session.GetTimelineProperties()
    if ($timeline) {
        $posMs = [int]$timeline.Position.TotalMilliseconds
        $durMs = [int]$timeline.EndTime.TotalMilliseconds
    }

    if ($props -and $props.Thumbnail) {
        $streamOp = $props.Thumbnail.OpenReadAsync()
        while ($streamOp.Status -eq 0) { Start-Sleep -Milliseconds 20 }
        $stream = $streamOp.GetResults()
        $netStream = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream)
        $outPath = Join-Path $PSScriptRoot 'current_thumb.jpg'
        $fs = [System.IO.File]::Create($outPath)
        $netStream.CopyTo($fs)
        $fs.Close()
        Write-Output "THUMB::$($props.Artist)::::$($props.Title)::::$posMs::::$durMs"
        exit 0
    } elseif ($props -and $props.Title) {
        Write-Output "NOTHUMB::$($props.Artist)::::$($props.Title)::::$posMs::::$durMs"
        exit 0
    }
}
Write-Output "IDLE"
