# Runs the translation quality test against the model CIMA Box has downloaded to this PC.
#
#   powershell -ExecutionPolicy Bypass -File tools\quality\run.ps1                 (on the processor)
#   powershell -ExecutionPolicy Bypass -File tools\quality\run.ps1 -Gpu            (on the graphics card)
#   powershell -ExecutionPolicy Bypass -File tools\quality\run.ps1 -Only gender    (only the cases with "gender" in their id or... see README)
#
# It starts its own copy of the engine, on the processor unless -Gpu is given, so it can't take
# graphics memory from a game or from CIMA Box. Close CIMA Box and any game before using -Gpu: a second
# model on a graphics card that is already busy runs many times slower and can fail.
param(
    [switch]$Gpu,
    [string]$Only = "",
    [string]$Model = "",
    [int]$Port = 8791
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$data = Join-Path $env:APPDATA "CIMA Box\local-models"
$engine = Join-Path $data "engine\llama-server.exe"
if (-not (Test-Path $engine)) { Write-Host "No engine found at $engine. Pick a model on this PC in CIMA Box first (Settings, translation service: Local)."; exit 2 }
if ($Model -eq "") {
    $first = Get-ChildItem (Join-Path $data "models") -Filter *.gguf | Sort-Object Length | Select-Object -First 1
    if (-not $first) { Write-Host "No model found in $data\models."; exit 2 }
    $Model = $first.FullName
}
$tool = Join-Path $root "build\Release\cima_quality.exe"
if (-not (Test-Path $tool)) { Write-Host "Build the test first:  cmake --build build --config Release --target cima_quality"; exit 2 }

$layers = if ($Gpu) { "999" } else { "0" }
$log = Join-Path $env:TEMP "cima-quality-engine.log"
$proc = Start-Process -FilePath $engine -ArgumentList @("-m", "`"$Model`"", "--host", "127.0.0.1", "--port", "$Port", "-ngl", $layers, "-c", "8192", "-np", "2") `
    -WorkingDirectory (Split-Path -Parent $engine) -RedirectStandardError $log -WindowStyle Hidden -PassThru
try {
    $up = $false
    for ($i = 0; $i -lt 240 -and -not $proc.HasExited; $i++) {
        try { if ((Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 "http://127.0.0.1:$Port/health").StatusCode -eq 200) { $up = $true; break } } catch { Start-Sleep -Milliseconds 500 }
    }
    if (-not $up) { Write-Host "The engine didn't start. See $log"; exit 2 }
    $out = Join-Path $root "tools\quality\report.txt"
    $toolArgs = @("--url", "http://127.0.0.1:$Port", "--cases", (Join-Path $root "tools\quality\cases.json"), "--out", $out, "--model", (Split-Path -Leaf $Model))
    if ($Only -ne "") { $toolArgs += @("--only", $Only) }
    & $tool @toolArgs
    $code = $LASTEXITCODE
} finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
exit $code
