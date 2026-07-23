[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$serviceName = 'CanonLBP810Bridge'
$printerName = 'Canon LBP-810'
$driverName = 'Microsoft PS Class Driver'
$portName = 'CanonLBP810_CAPT'
$packageRoot = Split-Path -Parent $PSScriptRoot
$installRoot = Join-Path $env:ProgramFiles 'Canon LBP-810 Bridge'
$dataRoot = Join-Path $env:ProgramData 'Canon LBP-810 Bridge'
$spool = Join-Path $dataRoot 'spool'
$bridge = Join-Path $installRoot 'bin\lbp810-bridge.exe'
$ghostscript = Join-Path $installRoot 'ghostscript\bin\gswin64c.exe'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator
    )) {
        throw 'Administrator privileges are required.'
    }
}

function Stop-BridgeService {
    $service = Get-CimInstance Win32_Service `
        -Filter "Name='$serviceName'" `
        -ErrorAction SilentlyContinue
    if (-not $service -or $service.State -eq 'Stopped') {
        return
    }

    & sc.exe stop $serviceName | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 500
        $service = Get-CimInstance Win32_Service -Filter "Name='$serviceName'"
    } while (
        $service.State -ne 'Stopped' -and
        [DateTime]::UtcNow -lt $deadline
    )

    if ($service.State -ne 'Stopped' -and $service.ProcessId -ne 0) {
        Stop-Process -Id $service.ProcessId -Force
        Start-Sleep -Seconds 1
    }
}

Assert-Administrator

$sourceBridge = Join-Path $packageRoot 'bin\lbp810-bridge.exe'
$sourceGhostscript = Join-Path $packageRoot 'ghostscript\bin\gswin64c.exe'
if (-not (Test-Path -LiteralPath $sourceBridge -PathType Leaf)) {
    throw "Package is incomplete: missing $sourceBridge"
}
if (-not (Test-Path -LiteralPath $sourceGhostscript -PathType Leaf)) {
    throw "Package is incomplete: missing $sourceGhostscript"
}

Stop-BridgeService

$expectedInstallRoot = [IO.Path]::GetFullPath(
    (Join-Path $env:ProgramFiles 'Canon LBP-810 Bridge')
)
if ([IO.Path]::GetFullPath($installRoot) -ne $expectedInstallRoot) {
    throw 'Internal installation path validation failed.'
}
if (Test-Path -LiteralPath $installRoot) {
    Remove-Item -LiteralPath $installRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $installRoot, $spool | Out-Null
Copy-Item -LiteralPath (Join-Path $packageRoot 'bin') `
    -Destination $installRoot -Recurse -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'ghostscript') `
    -Destination $installRoot -Recurse -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'licenses') `
    -Destination $installRoot -Recurse -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'README.txt') `
    -Destination $installRoot -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'THIRD-PARTY-NOTICES.txt') `
    -Destination $installRoot -Force

$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object InstanceId -Like 'USB\VID_04A9&PID_260A\*' |
    Select-Object -First 1
if (-not $device) {
    throw 'Canon LBP-810 was not found. Connect and power on USB\VID_04A9&PID_260A, then run the installer again.'
}

& pnputil.exe /restart-device $device.InstanceId | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Failed to restart the USB printer (pnputil: $LASTEXITCODE)."
}
Start-Sleep -Seconds 2

& $bridge --probe
if ($LASTEXITCODE -ne 0) {
    throw 'Windows found the LBP-810, but its CAPT interface is unavailable.'
}

$binaryPath = ('"{0}" --service --gs "{1}" --spool "{2}" --port 9100' -f
    $bridge, $ghostscript, $spool)
$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($service) {
    Set-ItemProperty `
        -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName" `
        -Name ImagePath `
        -Value $binaryPath
    Set-Service -Name $serviceName -StartupType Automatic
} else {
    New-Service `
        -Name $serviceName `
        -BinaryPathName $binaryPath `
        -DisplayName 'Canon LBP-810 Print Bridge' `
        -Description 'Converts PostScript jobs to Canon CAPT/SCoA over USB.' `
        -StartupType Automatic | Out-Null
}

if (-not (Get-PrinterDriver -Name $driverName -ErrorAction SilentlyContinue)) {
    Add-PrinterDriver -Name $driverName
}
if (-not (Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue)) {
    Add-PrinterPort `
        -Name $portName `
        -PrinterHostAddress '127.0.0.1' `
        -PortNumber 9100
}

$printer = Get-Printer -Name $printerName -ErrorAction SilentlyContinue
if ($printer) {
    Set-Printer `
        -Name $printerName `
        -DriverName $driverName `
        -PortName $portName
} else {
    Add-Printer `
        -Name $printerName `
        -DriverName $driverName `
        -PortName $portName
}
Set-PrintConfiguration `
    -PrinterName $printerName `
    -PaperSize A4 `
    -Color $false

Start-Service -Name $serviceName
$service = Get-Service -Name $serviceName
$printer = Get-Printer -Name $printerName

Write-Host ''
Write-Host 'Canon LBP-810 was installed for Windows 11 x64.'
Write-Host "Service: $($service.Status), automatic startup"
Write-Host "Queue: $($printer.Name)"
Write-Host "Log: $spool\bridge.log"
