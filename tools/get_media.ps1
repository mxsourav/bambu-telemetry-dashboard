Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
Function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}
[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media, ContentType = WindowsRuntime] | Out-Null
$asyncOp = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()
$mgr = Await $asyncOp ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
$session = $mgr.GetCurrentSession()
if ($session) {
    $propAsync = $session.TryGetMediaPropertiesAsync()
    $props = Await $propAsync ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
    $posMs = 0
    $durMs = 0
    $timeline = $session.GetTimelineProperties()
    if ($timeline) {
        $posMs = [int]$timeline.Position.TotalMilliseconds
        $durMs = [int]$timeline.EndTime.TotalMilliseconds
    }
    
    $thumbSaved = $false
    if ($props -and $props.Thumbnail) {
        try {
            $streamOp = $props.Thumbnail.OpenReadAsync()
            $stream = Await $streamOp ([Windows.Storage.Streams.IRandomAccessStreamWithContentType])
            $netStream = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream)
            $outPath = Join-Path $PSScriptRoot 'current_thumb.jpg'
            $fs = [System.IO.File]::Create($outPath)
            $netStream.CopyTo($fs)
            $fs.Close()
            $thumbSaved = $true
        } catch {
        }
    }
    if ($thumbSaved) {
        Write-Output "THUMB::$($props.Artist)::::$($props.Title)::::$posMs::::$durMs"
    } elseif ($props -and $props.Title) {
        Write-Output "NOTHUMB::$($props.Artist)::::$($props.Title)::::$posMs::::$durMs"
    } else {
        Write-Output "IDLE"
    }
} else {
    Write-Output "IDLE"
}
