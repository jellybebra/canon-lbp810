[CmdletBinding()]
param(
    [switch]$SkipDeviceRestart
)

$ErrorActionPreference = 'Stop'
$serviceName = 'CanonLBP810Bridge'
$printerName = 'Canon LBP-810'
$driverName = 'Microsoft PS Class Driver'
$portName = 'CanonLBP810_CAPT'
$root = Split-Path -Parent $PSScriptRoot
$bridge = Join-Path $root 'build-native\bin\lbp810-bridge.exe'
$ghostscript = Join-Path $root 'runtime\msys64\ucrt64\bin\gswin64c.exe'
$spool = Join-Path $root 'spool'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Запустите этот сценарий в PowerShell от имени администратора.'
}
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) {
    throw "Не найден мост печати: $bridge"
}
if (-not (Test-Path -LiteralPath $ghostscript -PathType Leaf)) {
    throw "Не найден Ghostscript: $ghostscript"
}

New-Item -ItemType Directory -Force -Path $spool | Out-Null

if (-not $SkipDeviceRestart) {
    $device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object InstanceId -Like 'USB\VID_04A9&PID_260A\*' |
        Select-Object -First 1
    if ($device) {
        & pnputil.exe /restart-device $device.InstanceId | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Не удалось перезапустить USB-устройство Canon (pnputil: $LASTEXITCODE)."
        }
        Start-Sleep -Seconds 2
    }
}

$binaryPath = ('"{0}" --service --gs "{1}" --spool "{2}" --port 9100' -f
    $bridge, $ghostscript, $spool)
$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -ne 'Stopped') {
        Stop-Service -Name $serviceName -Force
    }
    Set-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName" `
        -Name ImagePath -Value $binaryPath
    Set-Service -Name $serviceName -StartupType Automatic
} else {
    New-Service -Name $serviceName `
        -BinaryPathName $binaryPath `
        -DisplayName 'Canon LBP-810 Print Bridge' `
        -Description 'Converts local PostScript print jobs to Canon CAPT/SCoA over USB.' `
        -StartupType Automatic | Out-Null
}

if (-not (Get-PrinterDriver -Name $driverName -ErrorAction SilentlyContinue)) {
    Add-PrinterDriver -Name $driverName
}
if (-not (Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue)) {
    Add-PrinterPort -Name $portName `
        -PrinterHostAddress '127.0.0.1' `
        -PortNumber 9100
}

$printer = Get-Printer -Name $printerName -ErrorAction SilentlyContinue
if ($printer) {
    Set-Printer -Name $printerName -DriverName $driverName -PortName $portName
} else {
    Add-Printer -Name $printerName -DriverName $driverName -PortName $portName
}
Set-PrintConfiguration -PrinterName $printerName -PaperSize A4 -Color $false

Start-Service -Name $serviceName
$service = Get-Service -Name $serviceName
$printer = Get-Printer -Name $printerName

Write-Host ''
Write-Host 'Установка завершена.'
Write-Host "Служба: $($service.Name) [$($service.Status)]"
Write-Host "Принтер: $($printer.Name)"
Write-Host "Драйвер очереди: $($printer.DriverName)"
Write-Host "Порт: $($printer.PortName) -> 127.0.0.1:9100"
Write-Host "Журнал: $spool\bridge.log"
