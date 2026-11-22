[Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime] | Out-Null
$profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
if ($profile) {
    [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime] | Out-Null
    $mgr = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
    Write-Host "Current state:" $mgr.TetheringOperationalState
    if ($mgr.TetheringOperationalState -eq "Off") {
        Write-Host "Enabling Mobile Hotspot..."
        $asyncOp = $mgr.StartTetheringAsync()
        # Wait for async operation
        $asTaskGeneric = [System.WindowsRuntimeSystemExtensions].GetMethods() | ? { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' }
        $netOpResult = $asTaskGeneric.MakeGenericMethod([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult]).Invoke($null, @($asyncOp))
        $netOpResult.Wait()
        Write-Host "Result:" $netOpResult.Result.Status
        Write-Host "New state:" $mgr.TetheringOperationalState
    }
}
