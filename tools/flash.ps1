# Compila y flashea un firmware en la placa conectada por USB y abre el monitor serie.
#   tools\flash.bat demos\01-vr-cardboard\firmware
#   tools\flash.bat demos\03-espnow-balizas\baliza COM5 -Entorno baliza2
#   tools\flash.bat demos\01-vr-cardboard\firmware -NoMonitor
# Si no detecta el CoreS3: manten pulsado su boton RST ~3 s hasta que el LED se
# ponga verde (modo descarga) y vuelve a intentarlo.
param([string]$Project, [string]$Port, [string]$Entorno, [switch]$NoMonitor)
# Native tools report failure through $LASTEXITCODE; "Stop" would abort on any stderr output in PowerShell 5.1.
$ErrorActionPreference = "Continue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$Pio = Join-Path $Root ".venv\Scripts\pio.exe"
if (-not (Test-Path $Pio)) { Write-Host "Primero ejecuta setup.bat"; exit 1 }
if (-not (Test-Path "config\wifi_secrets.h")) { Write-Host "Falta la Wi-Fi: setup.bat -Wifi"; exit 1 }
if (-not $Project) {
    Write-Host "Uso: tools\flash.bat <carpeta-del-firmware> [puerto] [-NoMonitor]"
    Write-Host "Firmwares disponibles:"
    Get-ChildItem demos -Recurse -Filter platformio.ini | Where-Object { $_.FullName -notmatch '\\.pio\\' } |
        ForEach-Object { Write-Host "  $(Resolve-Path -Relative $_.DirectoryName)" }
    exit 1
}
if (-not (Test-Path (Join-Path $Project "platformio.ini"))) { Write-Host "No hay platformio.ini en $Project"; exit 1 }
$up = @(); $mon = @()
if ($Port) { $up = @("--upload-port", $Port); $mon = @("--port", $Port) }
if ($Entorno) { $up += @("-e", $Entorno); $mon += @("-e", $Entorno) }
& $Pio run --project-dir $Project -t upload @up
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $NoMonitor) {
    Write-Host "Monitor serie (Ctrl+C para salir)."
    & $Pio device monitor --project-dir $Project @mon
}
