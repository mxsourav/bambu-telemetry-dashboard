param([switch]$Stream)

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

$lastThumbHash = ""

do {
    $session = $mgr.GetCurrentSession()
    $playbackInfo = if ($session) { $session.GetPlaybackInfo() } else { $null }
    $isPlaying = if ($playbackInfo) { $playbackInfo.PlaybackStatus -eq [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus]::Playing } else { $false }

    # If current session is not playing, scan all sessions for one that is actively Playing (e.g. Phone Link or background player)
    if (-not $isPlaying) {
        $allSessions = $mgr.GetSessions()
        if ($allSessions) {
            foreach ($s in $allSessions) {
                $pb = $s.GetPlaybackInfo()
                if ($pb -and ($pb.PlaybackStatus -eq [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus]::Playing)) {
                    $session = $s
                    $playbackInfo = $pb
                    $isPlaying = $true
                    break
                }
            }
        }
    }

    $props = $null
    $posMs = 0
    $durMs = 0

    if ($session) {
        $propAsync = $session.TryGetMediaPropertiesAsync()
        $props = Await $propAsync ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])

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
        
        $artist = if ($props -and $props.Artist) { $props.Artist } else { "Spotify" }
        $title = if ($props -and $props.Title) { $props.Title } else { "Unknown" }
        $thumbKey = "${artist}-${title}"
        $thumbSaved = $false

        if ($props -and $props.Thumbnail -and ($thumbKey -ne $lastThumbHash)) {
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
                    $lastThumbHash = $thumbKey
                }
            } catch {
            }
        } elseif ($thumbKey -eq $lastThumbHash) {
            $thumbSaved = [System.IO.File]::Exists((Join-Path $PSScriptRoot 'current_thumb_raw.dat'))
        }

        $playTag = if ($isPlaying) { "PLAY" } else { "PAUSE" }
        $thumbTag = if ($thumbSaved) { "THUMB" } else { "NOTHUMB" }

        Write-Output "${thumbTag}::${playTag}::${artist}::::${title}::::${posMs}::::${durMs}"
    } else {
        Write-Output "IDLE"
    }

    [Console]::Out.Flush()

    if ($Stream) {
        Start-Sleep -Milliseconds 500
    }
} while ($Stream)
