[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$printerName = 'Canon LBP-810'
if (-not (Get-Printer -Name $printerName -ErrorAction SilentlyContinue)) {
    throw "Очередь '$printerName' не установлена."
}

$arguments = "printui.dll,PrintUIEntry /k /n `"$printerName`""
$process = Start-Process -FilePath "$env:SystemRoot\System32\rundll32.exe" `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru `
    -Wait
if ($process.ExitCode -ne 0) {
    throw "Не удалось отправить тестовую страницу Windows (код $($process.ExitCode))."
}
Write-Host "Тестовая страница отправлена в очередь '$printerName'."
