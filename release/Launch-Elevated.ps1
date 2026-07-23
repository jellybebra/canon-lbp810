[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Install.ps1', 'Uninstall.ps1')]
    [string]$ScriptName
)

$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot $ScriptName
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)

if ($isAdministrator) {
    & $target
    exit $LASTEXITCODE
}

$powershell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$target`""
$process = Start-Process `
    -FilePath $powershell `
    -Verb RunAs `
    -ArgumentList $arguments `
    -Wait `
    -PassThru
exit $process.ExitCode
