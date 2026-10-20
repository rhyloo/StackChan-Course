# Lo que hacen los run.bat de cada demo: comprueba que todo esta listo, sube el
# firmware al robot y arranca lo que la demo necesita en el portatil.
#   tools\run_demo.ps1 <demo> [-SinFlashear] [-Puerto COM5] [opciones de la pasarela]
#   tools\run_demo.ps1 03 balizas      # demo 03: flashea las tres balizas
# Equivale a tools/run_demo.sh (Linux/macOS): si cambias uno, cambia el otro.
param([Parameter(Mandatory = $true)][string]$Demo, [switch]$SinFlashear, [string]$Puerto,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Resto)
$ErrorActionPreference = "Continue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$Py = Join-Path $Root ".venv\Scripts\python.exe"
if (-not $Resto) { $Resto = @() }

function Say($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "`n$m" -ForegroundColor Red; exit 1 }

$Dir = Get-ChildItem demos -Directory | Where-Object { $_.Name -like "$($Demo -replace '^demos[\\/]', '')*" } | Select-Object -First 1
if (-not $Dir) { Fail "No existe la demo '$Demo'. Hay: $((Get-ChildItem demos -Directory).Name -join ' ')" }
$D = "demos\$($Dir.Name)"
if (-not (Test-Path $Py)) { Fail "Falta el entorno. Ejecuta primero: setup.bat" }
if (-not (Test-Path "config\wifi_secrets.h")) { Fail "Falta la Wi-Fi. Ejecuta: setup.bat -Wifi" }

function Need-GeminiKey {
    if (-not $env:GEMINI_API_KEY) {
        Fail ("Esta demo usa Gemini y falta la clave. Creala gratis en https://aistudio.google.com/apikey y ejecuta:`n" +
              "    `$env:GEMINI_API_KEY=`"tu-clave`"`n" +
              "(para no repetirlo: setx GEMINI_API_KEY `"tu-clave`" y abre otra terminal)")
    }
}

function Flash-Robot($fw) {
    if ($SinFlashear) { Say "Sin flashear (-SinFlashear): uso lo que ya tiene el robot"; return }
    $port = $Puerto
    if (-not $port) { $port = (& $Py tools\find_port.py) }
    if (-not $port) {
        Fail ("No encuentro el robot por USB.`n- Conectalo con un cable USB-C de datos (algunos solo cargan).`n" +
              "- Si sigue sin aparecer, manten pulsado RST unos 3 s (LED verde) y repite.")
    }
    Say "Compilando y subiendo $fw al robot ($port). La primera vez tarda unos minutos..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash.ps1 $fw $port -NoMonitor
    if ($LASTEXITCODE) {
        Fail ("No se pudo subir el firmware.`n- Tienes abierto un monitor serie? Cierralo: ocupa el puerto.`n" +
              "- Si no, manten pulsado RST unos 3 s (LED verde, modo descarga) y repite.")
    }
    Say "Firmware subido. El robot se reinicia (unos segundos)."
}

function Start-Relay {
    Say "Arrancando la pasarela del portatil (Ctrl+C para salir)"
    & $Py "$D\relay\main.py" @Resto
    exit $LASTEXITCODE
}

switch -Wildcard ($Dir.Name) {
    "01-*" { Flash-Robot "$D\firmware"; Start-Relay }
    "02-*" { Need-GeminiKey; Flash-Robot "$D\firmware"; Start-Relay }
    "05-*" { Need-GeminiKey; Flash-Robot "$D\firmware"; Start-Relay }
    "03-*" {
        if ($Resto.Count -gt 0 -and $Resto[0] -eq "balizas") {
            foreach ($n in 1..3) {
                Read-Host "Conecta SOLO la baliza $n (ESP32 DevKit) por USB y pulsa Enter"
                $port = $Puerto
                if (-not $port) { $port = (& $Py tools\find_port.py --devkit) }
                if (-not $port) { Fail "No veo ninguna ESP32 DevKit (CP210x o CH34x). Cable de datos? Driver?" }
                Say "Subiendo la baliza $n ($port)"
                & powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash.ps1 "$D\baliza" $port -Entorno "baliza$n" -NoMonitor
                if ($LASTEXITCODE) { exit $LASTEXITCODE }
            }
            Say "Balizas listas. Ahora: $D\run.bat"
            exit 0
        }
        Flash-Robot "$D\receptor"
        Say "Abriendo el mapa (Ctrl+C para salir). Para calibrar: $Py $D\portatil\posicion.py --calibrar 1"
        & $Py "$D\portatil\posicion.py" @Resto
        exit $LASTEXITCODE
    }
    "04-*" {
        Flash-Robot "$D\firmware"
        Say "Acerca una tarjeta NFC a la base del robot. Aqui veras lo que lee (Ctrl+C para salir)."
        $a = @("tools\serial_log.py", "$D\firmware"); if ($Puerto) { $a += $Puerto }
        & $Py @a
        exit $LASTEXITCODE
    }
    "08-*" {
        Flash-Robot "$D\firmware"
        Say "Mueve el servo desde la pantalla del robot (Ctrl+C para salir)."
        $a = @("tools\serial_log.py", "$D\firmware"); if ($Puerto) { $a += $Puerto }
        & $Py @a
        exit $LASTEXITCODE
    }
    "07-*" {
        if (-not $env:GEMINI_API_KEY) { Say "Sin GEMINI_API_KEY solo funcionan Original, Boceto y Pixel (los filtros con IA usan Gemini)." }
        Flash-Robot "$D\firmware"; Start-Relay
    }
    "06-*" {
        Flash-Robot "$D\firmware"
        Say "Apunta un mando al robot y pulsa botones. Aqui veras los codigos (Ctrl+C para salir)."
        $a = @("tools\serial_log.py", "$D\firmware"); if ($Puerto) { $a += $Puerto }
        & $Py @a
        exit $LASTEXITCODE
    }
    default { Fail "La demo $D todavia no tiene run.bat" }
}
