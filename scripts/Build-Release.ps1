[CmdletBinding()]
param(
    [string]$Version = '1.0.0'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$packageName = "Canon-LBP810-Windows11-x64-$Version"
$stage = Join-Path $dist $packageName
$zip = Join-Path $dist "$packageName.zip"
$bridge = Join-Path $root 'build-native\bin\lbp810-bridge.exe'
$runtimeBin = Join-Path $root 'runtime\msys64\ucrt64\bin'
$ghostscript = Join-Path $runtimeBin 'gswin64c.exe'
$objdump = Join-Path $runtimeBin 'objdump.exe'

foreach ($required in @($bridge, $ghostscript, $objdump)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

$expectedDist = [IO.Path]::GetFullPath((Join-Path $root 'dist'))
if ([IO.Path]::GetFullPath($dist) -ne $expectedDist) {
    throw 'Internal dist path validation failed.'
}
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}

$binOut = Join-Path $stage 'bin'
$gsBinOut = Join-Path $stage 'ghostscript\bin'
$gsShareOut = Join-Path $stage 'ghostscript\share\ghostscript\10.07.1'
$scriptsOut = Join-Path $stage 'scripts'
$licensesOut = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force `
    -Path $binOut, $gsBinOut, $gsShareOut, $scriptsOut, $licensesOut |
    Out-Null

Copy-Item -LiteralPath $bridge -Destination $binOut

$queue = [Collections.Generic.Queue[string]]::new()
$seen = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
$queue.Enqueue($ghostscript)
while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    if (-not $seen.Add($file)) {
        continue
    }
    $imports = & $objdump -p $file |
        Select-String 'DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
    foreach ($name in $imports) {
        $candidate = Join-Path $runtimeBin $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $queue.Enqueue($candidate)
        }
    }
}
foreach ($file in $seen) {
    Copy-Item -LiteralPath $file -Destination $gsBinOut
}

Copy-Item `
    -LiteralPath (Join-Path $root 'runtime\msys64\ucrt64\share\ghostscript\10.07.1\lib') `
    -Destination $gsShareOut `
    -Recurse
Copy-Item `
    -LiteralPath (Join-Path $root 'runtime\msys64\ucrt64\share\licenses') `
    -Destination (Join-Path $licensesOut 'msys2') `
    -Recurse
Copy-Item `
    -LiteralPath (Join-Path $root 'third_party\captppd\LICENSE') `
    -Destination (Join-Path $licensesOut 'captppd-LICENSE.txt')
Copy-Item `
    -LiteralPath (Join-Path $root 'third_party\libcapt\LICENSE') `
    -Destination (Join-Path $licensesOut 'libcapt-LICENSE.txt')

Copy-Item -LiteralPath (Join-Path $root 'release\Install.ps1') `
    -Destination $scriptsOut
Copy-Item -LiteralPath (Join-Path $root 'release\Uninstall.ps1') `
    -Destination $scriptsOut
Copy-Item -LiteralPath (Join-Path $root 'release\Print-TestPage.ps1') `
    -Destination $scriptsOut
Copy-Item -LiteralPath (Join-Path $root 'release\Launch-Elevated.ps1') `
    -Destination $scriptsOut
Copy-Item -LiteralPath (Join-Path $root 'release\Install.cmd') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'release\Uninstall.cmd') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'release\Print-TestPage.cmd') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'release\README.txt') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'release\THIRD-PARTY-NOTICES.txt') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') `
    -Destination (Join-Path $stage 'LICENSE.txt')

Set-Content `
    -LiteralPath (Join-Path $stage 'VERSION') `
    -Value $Version `
    -Encoding ascii

Compress-Archive `
    -LiteralPath $stage `
    -DestinationPath $zip `
    -CompressionLevel Optimal

$files = Get-ChildItem -LiteralPath $stage -Recurse -File
$hash = Get-FileHash -LiteralPath $zip -Algorithm SHA256
Write-Host "Package: $zip"
Write-Host "Files: $($files.Count)"
Write-Host "Unpacked size: $([Math]::Round(
    ($files | Measure-Object Length -Sum).Sum / 1MB,
    2
)) MiB"
Write-Host "ZIP size: $([Math]::Round((Get-Item $zip).Length / 1MB, 2)) MiB"
Write-Host "SHA256: $($hash.Hash)"
