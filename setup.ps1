# Instala TODO lo necesario para programar Stack-chan en Windows 10/11,
# partiendo de un ordenador sin nada: Git, Python, VS Code + PlatformIO, la
# toolchain del ESP32-S3 y el entorno del proyecto.
#
#   Doble clic en setup.bat, o en PowerShell:
#   powershell -ExecutionPolicy Bypass -File setup.ps1 [-Wifi] [-Flash] [-NoSystem]
#
# Se puede ejecutar las veces que haga falta: lo ya instalado se salta.
param([switch]$Wifi, [switch]$Flash, [switch]$NoSystem)
# Native tools report failure through $LASTEXITCODE; "Stop" would abort on any stderr output in PowerShell 5.1.
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot
$Root = $PSScriptRoot
$Venv = Join-Path $Root ".venv"
$Py = Join-Path $Venv "Scripts\python.exe"
$Pio = Join-Path $Venv "Scripts\pio.exe"
$Secrets = Join-Path $Root "config\wifi_secrets.h"

function Bold($m) { Write-Host "`n$m" -ForegroundColor White }
function Ok($m)   { Write-Host "  [OK] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  [!]  $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "  [X]  $m" -ForegroundColor Red; exit 1 }
function Have($c) { [bool](Get-Command $c -ErrorAction SilentlyContinue) }
function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

function Ask-Wifi {
    Bold "Wi-Fi del robot"
    Write-Host "  El robot, el portatil y el movil tienen que estar en la MISMA red Wi-Fi de 2,4 GHz."
    $ssid = Read-Host "  Nombre de la red (SSID)"
    if (-not $ssid) { Die "El SSID no puede estar vacio" }
    $sec = Read-Host "  Contrasena (no se muestra)" -AsSecureString
    $pass = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
    function CStr($s) { '"' + $s.Replace('\', '\\').Replace('"', '\"') + '"' }
    $content = "// Generado por setup. NO lo subas al repo (esta en .gitignore).`n#pragma once`n" +
               "#define WIFI_SSID $(CStr $ssid)`n#define WIFI_PASS $(CStr $pass)`n"
    [IO.File]::WriteAllText($Secrets, $content, (New-Object Text.UTF8Encoding $false))
    Ok "Guardada en config\wifi_secrets.h (la usan todos los firmwares)"
}

if ($Wifi) {
    if (-not (Test-Path $Py)) { Die "Primero ejecuta setup.bat" }
    Ask-Wifi
    exit 0
}

if (-not $NoSystem) {
    Bold "1/6 Programas (winget)"
    if (-not (Have winget)) { Die "Falta winget. Instala 'App Installer' desde Microsoft Store y vuelve a ejecutar." }
    $apps = @(
        @{ Id = "Git.Git"; Cmd = "git"; Name = "Git" },
        @{ Id = "Python.Python.3.12"; Cmd = "py"; Name = "Python 3.12" },
        @{ Id = "Microsoft.VisualStudioCode"; Cmd = "code"; Name = "Visual Studio Code" }
    )
    foreach ($a in $apps) {
        if (Have $a.Cmd) { Ok "$($a.Name) ya instalado"; continue }
        Write-Host "  Instalando $($a.Name)..."
        winget install --id $a.Id -e --silent --accept-package-agreements --accept-source-agreements | Out-Null
        Refresh-Path
        if (Have $a.Cmd) { Ok "$($a.Name)" } else { Warn "$($a.Name) instalado; si no lo encuentra, cierra y abre la terminal y repite" }
    }
    if (Have code) {
        foreach ($ext in @("platformio.platformio-ide", "ms-python.python")) {
            code --install-extension $ext --force 2>$null | Out-Null
        }
        Ok "Extensiones de VS Code: PlatformIO IDE y Python"
    }

    # El CoreS3 usa el USB nativo del ESP32-S3 (303A:1001): Windows 10/11 suele
    # instalar el driver solo (con Internet). Si falla, ver docs/ROBOT.md.

    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($admin) {
        if (-not (Get-NetFirewallRule -DisplayName "Stack-chan VR" -ErrorAction SilentlyContinue)) {
            New-NetFirewallRule -DisplayName "Stack-chan VR" -Direction Inbound -Protocol UDP -LocalPort 5600-5601 -Action Allow -Profile Private,Domain | Out-Null
        }
        Ok "Cortafuegos: UDP 5600-5601 abiertos en redes privadas"
    } else {
        Warn "Cortafuegos: la primera vez que arranques la pasarela, Windows preguntara. Marca 'Redes privadas' y pulsa Permitir."
        Warn "Y comprueba que tu Wi-Fi esta como red PRIVADA (Configuracion > Red > Wi-Fi > propiedades)."
    }
}

Bold "2/6 Entorno de Python del proyecto (.venv)"
Refresh-Path
if (-not (Test-Path $Py)) {
    if (Have py) { py -3 -m venv $Venv } elseif (Have python) { python -m venv $Venv } else { Die "No encuentro Python. Cierra y abre la terminal y repite." }
}
& $Py -m pip install --quiet --upgrade pip
& $Py -m pip install --quiet -r requirements.txt
if ($LASTEXITCODE) { Die "Fallo instalando dependencias de Python" }
Ok "platformio, opencv-python, numpy, pyserial"

Bold "3/6 Toolchains de ESP32 y ESP32-S3 (descarga ~1 GB la primera vez)"
$Projects = Get-ChildItem demos -Recurse -Filter platformio.ini | Where-Object { $_.FullName -notmatch '\\.pio\\' } | ForEach-Object { $_.DirectoryName }
foreach ($p in $Projects) {
    & $Pio pkg install --project-dir $p | Out-Null
    if ($LASTEXITCODE) { Die "Fallo descargando la toolchain de $p" }
}
Ok "Compiladores, framework Arduino-ESP32 y librerias de todas las demos"

Bold "4/6 Wi-Fi del robot"
if (Test-Path $Secrets) { Ok "Ya configurada. Para cambiarla: setup.bat -Wifi" } else { Ask-Wifi }

Bold "5/6 Comprobacion"
& $Py -m unittest discover -s lib/python/tests 2>$null | Out-Null
if ($LASTEXITCODE) { Warn "Fallan los tests de la pasarela" } else { Ok "Tests de la pasarela" }
foreach ($p in $Projects) {
    & $Pio run --project-dir $p --silent | Out-Null
    if ($LASTEXITCODE) { Die "No compila ${p}: .venv\Scripts\pio run -d $p" }
    Ok "Compila $(Resolve-Path -Relative $p)"
}

Bold "6/6 Robot por USB"
$ports = & $Pio device list --json-output 2>$null | ConvertFrom-Json | Where-Object { $_.hwid -match "303A" }
if ($ports) { Ok "Robot detectado en $($ports[0].port)" } else {
    Warn "No veo el robot por USB (normal si no esta conectado)."
    Warn "Si esta conectado y en el Administrador de dispositivos sale 'USB JTAG/serial debug unit' sin puerto COM,"
    Warn "falta el driver: ver docs\ROBOT.md, apartado 'Puerto USB en cada sistema'."
}
if ($Flash) { & (Join-Path $Root "tools\flash.ps1") -Project "demos\01-vr-cardboard\firmware" }

Bold "Listo!"
Write-Host "  Abrir el proyecto en VS Code:  code stackchan-curso.code-workspace"
Write-Host "  Primera demo:                   demos\01-vr-cardboard\README.md"
Write-Host "  Flashear un firmware:           tools\flash.bat   (sin argumentos lista los que hay)"
Write-Host "  Asistente de IA (opcional):     irm https://claude.ai/install.ps1 | iex   (Claude Code)"
Write-Host "  Demo 02 (voz):                  necesita GEMINI_API_KEY, ver demos\02-voz-gemini\README.md"
