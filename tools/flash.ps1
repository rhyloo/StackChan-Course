# Compila y flashea el firmware en el robot conectado por USB, y abre el monitor serie.
#   tools\flash.bat            autodetecta el puerto
#   tools\flash.bat COM5
#   tools\flash.bat -NoMonitor
# Si no lo detecta: manten pulsado el boton de reset del CoreS3 ~3 s hasta que
# se encienda el LED verde (modo descarga) y vuelve a intentarlo.
param([string]$Port, [switch]$NoMonitor)
# Native tools report failure through $LASTEXITCODE; "Stop" would abort on any stderr output in PowerShell 5.1.
$ErrorActionPreference = "Continue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$Pio = Join-Path $Root ".venv\Scripts\pio.exe"
if (-not (Test-Path $Pio)) { Write-Host "Primero ejecuta setup.bat"; exit 1 }
if (-not (Test-Path "firmware\include\wifi_secrets.h")) { Write-Host "Falta la Wi-Fi: setup.bat -Wifi"; exit 1 }
$up = @(); $mon = @()
if ($Port) { $up = @("--upload-port", $Port); $mon = @("--port", $Port) }
& $Pio run --project-dir firmware -t upload @up
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $NoMonitor) {
    Write-Host "Monitor serie (Ctrl+C para salir). Escribe 'info' + Enter para ver el estado."
    & $Pio device monitor --project-dir firmware @mon
}
