[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$serviceName = 'CanonLBP810Bridge'
$printerName = 'Canon LBP-810'
$portName = 'CanonLBP810_CAPT'
$installRoot = Join-Path $env:ProgramFiles 'Canon LBP-810 Bridge'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)) {
    throw 'Administrator privileges are required.'
}

if (Get-Printer -Name $printerName -ErrorAction SilentlyContinue) {
    Remove-Printer -Name $printerName
}
if (Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue) {
    Remove-PrinterPort -Name $portName
}

$service = Get-CimInstance Win32_Service `
    -Filter "Name='$serviceName'" `
    -ErrorAction SilentlyContinue
if ($service) {
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
    }
    & sc.exe delete $serviceName | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to delete the service (sc.exe: $LASTEXITCODE)."
    }
}

$expectedInstallRoot = [IO.Path]::GetFullPath(
    (Join-Path $env:ProgramFiles 'Canon LBP-810 Bridge')
)
if ([IO.Path]::GetFullPath($installRoot) -ne $expectedInstallRoot) {
    throw 'Internal removal path validation failed.'
}
if (Test-Path -LiteralPath $installRoot) {
    Remove-Item -LiteralPath $installRoot -Recurse -Force
}

Write-Host 'The queue, port, service, and program files were removed.'
Write-Host 'Diagnostic logs in ProgramData were preserved.'
