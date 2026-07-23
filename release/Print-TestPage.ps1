[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$printerName = 'Canon LBP-810'
if (-not (Get-Printer -Name $printerName -ErrorAction SilentlyContinue)) {
    throw "The '$printerName' queue is not installed."
}

$arguments = "printui.dll,PrintUIEntry /k /n `"$printerName`""
$process = Start-Process `
    -FilePath "$env:SystemRoot\System32\rundll32.exe" `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru `
    -Wait
if ($process.ExitCode -ne 0) {
    throw "Failed to submit the test page (code $($process.ExitCode))."
}
Write-Host "A Windows test page was submitted to '$printerName'."
