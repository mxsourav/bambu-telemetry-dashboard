
[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media, ContentType = WindowsRuntime] | Out-Null
 = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()
while (.Status -eq 0) { Start-Sleep -Milliseconds 20 }
 = .GetResults()
 = .GetCurrentSession()
if () {
     = .TryGetMediaPropertiesAsync()
    while (.Status -eq 0) { Start-Sleep -Milliseconds 20 }
     = .GetResults()
    if ( -and .Thumbnail) {
         = .Thumbnail.OpenReadAsync()
        while (.Status -eq 0) { Start-Sleep -Milliseconds 20 }
         = .GetResults()
         = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead()
         = [System.IO.File]::Create('tools/thumb.jpg')
        .CopyTo()
        .Close()
        Write-Output 'Thumbnail saved!'
    } else {
        Write-Output 'No thumbnail in props'
    }
} else {
    Write-Output 'No session'
}
