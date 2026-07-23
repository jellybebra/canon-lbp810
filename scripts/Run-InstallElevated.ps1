$ErrorActionPreference = 'Stop'
$log = Join-Path (Split-Path -Parent $PSScriptRoot) 'install-elevated.log'

try {
    & (Join-Path $PSScriptRoot 'Install-Lbp810.ps1') *>&1 |
        Tee-Object -FilePath $log
    exit 0
} catch {
    $_ | Out-String | Add-Content -LiteralPath $log
    exit 1
}
