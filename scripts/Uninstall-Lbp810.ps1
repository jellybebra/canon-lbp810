[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$serviceName = 'CanonLBP810Bridge'
$printerName = 'Canon LBP-810'
$portName = 'CanonLBP810_CAPT'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Запустите этот сценарий в PowerShell от имени администратора.'
}

if (Get-Printer -Name $printerName -ErrorAction SilentlyContinue) {
    Remove-Printer -Name $printerName
}
if (Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue) {
    Remove-PrinterPort -Name $portName
}

$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -ne 'Stopped') {
        Stop-Service -Name $serviceName -Force
    }
    & sc.exe delete $serviceName | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Не удалось удалить службу (sc.exe: $LASTEXITCODE)."
    }
}

Write-Host 'Очередь, порт и служба Canon LBP-810 удалены.'
Write-Host 'Исходный драйвер Canon, файлы проекта и журнал не удалялись.'
