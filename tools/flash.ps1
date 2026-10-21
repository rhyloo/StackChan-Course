# Builds and flashes a firmware to the board connected over USB and opens the serial monitor.
#   tools\flash.bat demos\01-vr-cardboard\firmware
#   tools\flash.bat demos\03-espnow-beacons\beacon COM5 -Env beacon2
#   tools\flash.bat demos\01-vr-cardboard\firmware -NoMonitor
# If the CoreS3 is not detected: hold its RST button ~3 s until the LED turns
# green (download mode) and try again.
param([string]$Project, [string]$Port, [string]$Env, [switch]$NoMonitor)
# Native tools report failure through $LASTEXITCODE; "Stop" would abort on any stderr output in PowerShell 5.1.
$ErrorActionPreference = "Continue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$Pio = Join-Path $Root ".venv\Scripts\pio.exe"
if (-not (Test-Path $Pio)) { Write-Host "Run setup.bat first"; exit 1 }
if (-not (Test-Path "config\wifi_secrets.h")) { Write-Host "The Wi-Fi settings are missing: setup.bat -Wifi"; exit 1 }
if (-not $Project) {
    Write-Host "Usage: tools\flash.bat <firmware-folder> [port] [-Env ENV] [-NoMonitor]"
    Write-Host "Available firmwares:"
    Get-ChildItem demos -Recurse -Filter platformio.ini | Where-Object { $_.FullName -notmatch '\\.pio\\' } |
        ForEach-Object { Write-Host "  $(Resolve-Path -Relative $_.DirectoryName)" }
    exit 1
}
if (-not (Test-Path (Join-Path $Project "platformio.ini"))) { Write-Host "There is no platformio.ini in $Project"; exit 1 }
$up = @(); $mon = @()
if ($Port) { $up = @("--upload-port", $Port); $mon = @("--port", $Port) }
if ($Env) { $up += @("-e", $Env); $mon += @("-e", $Env) }
& $Pio run --project-dir $Project -t upload @up
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $NoMonitor) {
    Write-Host "Serial monitor (Ctrl+C to quit)."
    & $Pio device monitor --project-dir $Project @mon
}
