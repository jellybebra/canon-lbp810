$ErrorActionPreference = 'Stop'
$serviceName = 'CanonLBP810Bridge'

$service = Get-CimInstance Win32_Service -Filter "Name='$serviceName'" `
    -ErrorAction SilentlyContinue
if ($service) {
    & sc.exe stop $serviceName | Out-Null
    Start-Sleep -Seconds 3
    $service = Get-CimInstance Win32_Service -Filter "Name='$serviceName'"
    if ($service.State -ne 'Stopped' -and $service.ProcessId -ne 0) {
        Stop-Process -Id $service.ProcessId -Force
        Start-Sleep -Seconds 1
    }
}

$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object InstanceId -Like 'USB\VID_04A9&PID_260A\*' |
    Select-Object -First 1
if ($device) {
    & pnputil.exe /restart-device $device.InstanceId | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "pnputil failed with exit code $LASTEXITCODE"
    }
}
