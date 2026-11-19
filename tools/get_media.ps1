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
    $playbackInfo = $session.GetPlaybackInfo()
    $isPlaying = $true
    if ($playbackInfo) {
        $isPlaying = ($playbackInfo.PlaybackStatus -eq [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus]::Playing)
    }

    $timeline = $session.GetTimelineProperties()
    if ($timeline) {
        $durMs = [int][Math]::Round($timeline.EndTime.TotalMilliseconds)
        $basePos = $timeline.Position.TotalMilliseconds
        if ($isPlaying -and $timeline.LastUpdatedTime) {
            $elapsed = ([DateTimeOffset]::Now - $timeline.LastUpdatedTime).TotalMilliseconds
            $posMs = [int][Math]::Round($basePos + $elapsed)
        } else {
            $posMs = [int][Math]::Round($basePos)
        }
        if ($durMs -gt 0 -and $posMs -gt $durMs) { $posMs = $durMs }
        if ($posMs -lt 0) { $posMs = 0 }
    }
    
    $thumbSaved = $false
    if ($props -and $props.Thumbnail) {
        try {
            $streamOp = $props.Thumbnail.OpenReadAsync()
            $stream = Await $streamOp ([Windows.Storage.Streams.IRandomAccessStreamWithContentType])
            $method = [System.IO.WindowsRuntimeStreamExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsStreamForRead' -and $_.GetParameters().Count -eq 1 } | Select-Object -First 1
            $netStream = $method.Invoke($null, @($stream))
            if ($netStream) {
                $outPath = Join-Path $PSScriptRoot 'current_thumb_raw.dat'
                $fs = [System.IO.File]::Create($outPath)
                $netStream.CopyTo($fs)
                $fs.Close()
                $netStream.Close()
                $thumbSaved = $true
            }
        } catch {
        }
    }

    $artist = if ($props -and $props.Artist) { $props.Artist } else { "Spotify" }
    $title = if ($props -and $props.Title) { $props.Title } else { "Unknown" }
    $playTag = if ($isPlaying) { "PLAY" } else { "PAUSE" }
    $thumbTag = if ($thumbSaved) { "THUMB" } else { "NOTHUMB" }

    Write-Output "${thumbTag}::${playTag}::${artist}::::${title}::::${posMs}::::${durMs}"
} else {
    Write-Output "IDLE"
}
