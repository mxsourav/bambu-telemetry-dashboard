# tools/test_thumb.ps1
# Utility to test Windows System Media Transport Controls (SMTC) thumbnail extraction.

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
    if ($props -and $props.Thumbnail) {
        $streamOp = $props.Thumbnail.OpenReadAsync()
        $stream = Await $streamOp ([Windows.Storage.Streams.IRandomAccessStreamWithContentType])
        $method = [System.IO.WindowsRuntimeStreamExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsStreamForRead' -and $_.GetParameters().Count -eq 1 } | Select-Object -First 1
        $netStream = $method.Invoke($null, @($stream))
        if ($netStream) {
            $outPath = Join-Path $PSScriptRoot 'thumb.jpg'
            $fs = [System.IO.File]::Create($outPath)
            $netStream.CopyTo($fs)
            $fs.Close()
            $netStream.Close()
            Write-Host "Thumbnail saved successfully to $outPath ($((Get-Item $outPath).Length) bytes)"
        }
    } else {
        Write-Host "No thumbnail found in active media session properties."
    }
} else {
    Write-Host "No active media session detected on system."
}
