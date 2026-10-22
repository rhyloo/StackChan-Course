# What each demo's run.bat does: checks that everything is ready, flashes the
# firmware to the robot and starts what the demo needs on the laptop.
#   tools\run_demo.ps1 <demo> [mode] [-Usb COM5] [relay options]
# Modes:
#   (none)         flash the firmware and start the demo
#   -FlashOnly     only build and flash the firmware; it stays in the robot's flash
#   -DebugLogs     like no mode, but save the robot's serial (logs\robot-*.log)
#                  and the relay with full detail (logs\relay-*.log)
#   -NoFlash       start the demo with what the robot already has
#   tools\run_demo.ps1 03 beacons      # demo 03: flash the three beacons
# Equivalent to tools/run_demo.sh (Linux/macOS): if you change one, change the other.
param([Parameter(Mandatory = $true)][string]$Demo, [switch]$NoFlash, [switch]$FlashOnly, [switch]$DebugLogs,
      [switch]$Help, [string]$Usb, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = "Continue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$Py = Join-Path $Root ".venv\Scripts\python.exe"
if (-not $Rest) { $Rest = @() }
$Fw = ""   # firmware flashed on the robot (for serial_log.py to decode crashes)

function Say($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "`n$m" -ForegroundColor Red; exit 1 }

# "02" must be demo 02, not 02.1: try the exact number first.
$Wanted = $Demo -replace '^demos[\\/]', ''
$Dir = Get-ChildItem demos -Directory | Where-Object { $_.Name -like "$Wanted-*" } | Select-Object -First 1
if (-not $Dir) { $Dir = Get-ChildItem demos -Directory | Where-Object { $_.Name -like "$Wanted*" } | Select-Object -First 1 }
if (-not $Dir) { Fail "Demo '$Demo' does not exist. Available: $((Get-ChildItem demos -Directory).Name -join ' ')" }
$D = "demos\$($Dir.Name)"
# -Help / --help: the modes, then the options of the program the demo runs on the laptop.
if ($Help -or $Rest -contains "--help") {
    Write-Host "Usage: $D\run.bat [mode] [-Usb PORT] [options]`n"
    Write-Host "Modes:"
    Write-Host "  (none)       flash the firmware and start the demo"
    Write-Host "  -FlashOnly   only build and flash the firmware; it stays on the robot"
    Write-Host "  -DebugLogs   like no mode, and save logs\robot-*.log (robot serial) and logs\relay-*.log"
    Write-Host "  -NoFlash     start the demo with what the robot already has"
    Write-Host "  -Usb PORT    USB port of the board (default: found automatically)"
    Write-Host "  -Help        show this help"
    $laptop = $null
    switch -Wildcard ($Dir.Name) {
        "01-*" { $laptop = "$D\relay\main.py" } "02-*" { $laptop = "$D\relay\main.py" } "02.1-*" { $laptop = "$D\relay\main.py" } "05-*" { $laptop = "$D\relay\main.py" }
        "07-*" { $laptop = "$D\relay\main.py" } "10-*" { $laptop = "$D\relay\main.py" }
        "03-*" { $laptop = "$D\laptop\position.py"; Write-Host "`nAlso:`n  $D\run.bat beacons   flash the three ESP32 beacons, one after another" }
        "12-*" { $laptop = "$D\laptop\tv.py"; Write-Host "`nAlso, without flashing:`n  $D\run.bat channels | add VIDEO | list | remove NAME" }
        "11-*" { $laptop = "$D\laptop\drive.py"
                 Write-Host "`nAlso:`n  $D\run.bat tank      flash the tank's Arduino (remove the HM-10 first)"
                 Write-Host "The robot (optional, for the L2/R2 sounds) gets demo 02's firmware; -DebugLogs does nothing here." }
    }
    if (-not $laptop) { Write-Host "`nThis demo has nothing to run on the laptop: it shows the robot's serial output." }
    elseif (Test-Path $Py) { Write-Host "`nAny other option goes to ${laptop}:`n"; & $Py $laptop --help }
    exit 0
}
if (-not (Test-Path $Py)) { Fail "The environment is missing. Run first: setup.bat" }
if (-not (Test-Path "config\wifi_secrets.h")) { Fail "The Wi-Fi settings are missing. Run: setup.bat -Wifi" }
if ($FlashOnly -and $NoFlash) { Fail "-FlashOnly and -NoFlash don't go together." }

function Need-GeminiKey {
    if ($FlashOnly) { return }
    if (-not $env:GEMINI_API_KEY) {
        Fail ("This demo uses Gemini and the key is missing. Create one for free at https://aistudio.google.com/apikey and run:`n" +
              "    `$env:GEMINI_API_KEY=`"your-key`"`n" +
              "(to avoid repeating it: setx GEMINI_API_KEY `"your-key`" and open a new terminal)")
    }
}

# With -FlashOnly it stops here.
function Flash-Robot($fw) {
    $script:Fw = $fw
    if ($NoFlash) { Say "Not flashing (-NoFlash): using what the robot already has"; return }
    $dev = $Usb
    if (-not $dev) { $dev = (& $Py tools\find_port.py) }
    if (-not $dev) {
        Fail ("I can't find the robot on USB.`n- Connect it with a USB-C data cable (some cables only charge).`n" +
              "- If it still doesn't show up, hold RST for about 3 s (green LED) and try again.")
    }
    Say "Building and flashing $fw to the robot ($dev). The first time takes a few minutes..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash.ps1 $fw $dev -NoMonitor
    if ($LASTEXITCODE) {
        Fail ("Could not flash the firmware.`n- Do you have a serial monitor open? Close it: it holds the port.`n" +
              "- If not, hold RST for about 3 s (green LED, download mode) and try again.")
    }
    if ($FlashOnly) {
        Say "Firmware flashed. It stays on the robot even if you unplug it or switch it off."
        if (Test-Path "$D\relay") { Write-Host "This demo needs the laptop relay: $D\run.bat -NoFlash" }
        exit 0
    }
    Say "Firmware flashed. The robot restarts (a few seconds)."
}

# Starts the laptop side. With -DebugLogs the robot's serial goes to logs\robot-*.log in the background.
function Run-Laptop([string[]]$a) {
    $bg = $null
    if ($DebugLogs -and $Fw) {
        $sa = @("tools\serial_log.py", $Fw); if ($Usb) { $sa += $Usb }
        $bg = Start-Process -FilePath $Py -ArgumentList $sa -WindowStyle Hidden -PassThru
        Say "Debugging: the robot's serial goes to logs\robot-*.log"
    }
    try { & $Py @a; $code = $LASTEXITCODE }
    finally { if ($bg) { Stop-Process -Id $bg.Id -ErrorAction SilentlyContinue } }
    exit $code
}

function Start-Relay {
    Say "Starting the laptop relay (Ctrl+C to quit)"
    $a = @("$D\relay\main.py"); if ($DebugLogs) { $a += "--debug" }
    Run-Laptop ($a + $Rest)
}

# Demos without a relay: the serial monitor already saves to logs\robot-*.log.
function Monitor($m) {
    Say "$m Here you'll see what the robot says; it is saved to logs\robot-*.log (Ctrl+C to quit)."
    $a = @("tools\serial_log.py", $Fw); if ($Usb) { $a += $Usb }
    & $Py @a
    exit $LASTEXITCODE
}

switch -Wildcard ($Dir.Name) {
    "01-*" { Flash-Robot "$D\firmware"; Start-Relay }
    "02-*" { Need-GeminiKey; Flash-Robot "$D\firmware"; Start-Relay }
    "02.1-*" { Need-GeminiKey; Flash-Robot "$D\firmware"; Start-Relay }
    "05-*" { Need-GeminiKey; Flash-Robot "$D\firmware"; Start-Relay }
    "03-*" {
        if ($Rest.Count -gt 0 -and $Rest[0] -eq "beacons") {
            foreach ($n in 1..3) {
                Read-Host "Connect ONLY beacon $n (ESP32 DevKit) over USB and press Enter"
                $dev = $Usb
                if (-not $dev) { $dev = (& $Py tools\find_port.py --devkit) }
                if (-not $dev) { Fail "I can't see any ESP32 DevKit (CP210x or CH34x). Data cable? Driver?" }
                Say "Flashing beacon $n ($dev)"
                & powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash.ps1 "$D\beacon" $dev -Env "beacon$n" -NoMonitor
                if ($LASTEXITCODE) { exit $LASTEXITCODE }
            }
            Say "Beacons ready. Now: $D\run.bat"
            exit 0
        }
        Flash-Robot "$D\receiver"
        Say "Opening the map (Ctrl+C to quit). To calibrate: $Py $D\laptop\position.py --calibrate 1"
        Run-Laptop (@("$D\laptop\position.py") + $Rest)
    }
    "04-*" { Flash-Robot "$D\firmware"; Monitor "Hold an NFC card near the robot's base." }
    "06-*" { Flash-Robot "$D\firmware"; Monitor "Point a remote control at the robot and press buttons." }
    "07-*" {
        if (-not $env:GEMINI_API_KEY -and -not $FlashOnly) { Say "Without GEMINI_API_KEY only Original, Sketch and Pixel work (the AI filters use Gemini)." }
        Flash-Robot "$D\firmware"; Start-Relay
    }
    "08-*" { Flash-Robot "$D\firmware"; Monitor "Move the servo from the robot's screen." }
    "09-*" { Flash-Robot "$D\firmware"; Monitor "Choose the module type on the robot's screen." }
    "13-*" { Flash-Robot "$D\firmware"; Monitor "Open the address it prints (or tap the robot's screen for a QR code) on a phone or laptop on the same Wi-Fi." }
    "10-*" { Need-GeminiKey; Flash-Robot "demos\02-gemini-voice\firmware"; Start-Relay }
    "11-*" {
        # "run.bat tank": the tank's Arduino. Otherwise the robot (optional: the L2/R2 sounds) gets demo 02's firmware.
        if ($Rest.Count -gt 0 -and $Rest[0] -eq "tank") {
            Read-Host "REMOVE the Bluetooth module (HM-10) from the tank, connect the Arduino over USB and press Enter"
            $dev = $Usb
            if (-not $dev) { $dev = (& $Py tools\find_port.py --arduino) }
            if (-not $dev) { Fail "I can't see the tank's Arduino on USB. Data cable? Board powered?" }
            Say "Flashing the firmware to the tank ($dev)"
            & powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash.ps1 "$D\tank" $dev -NoMonitor
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
            Say "Done. Put the Bluetooth module back and switch the tank on. Then: $D\run.bat"
            exit 0
        }
        $skip = $NoFlash
        if (-not $skip -and -not $Usb -and -not (& $Py tools\find_port.py)) {
            if ($FlashOnly) { Fail "I can't find the robot on USB." }
            Say "No robot on USB: driving without it. The L2/R2 sounds need it with demo 02's firmware."
            $skip = $true
        }
        if (-not $skip) { Flash-Robot "demos\02-gemini-voice\firmware" }
        Say "Drive with the PS4 gamepad; L2 horn, R2 blaster on the robot (Ctrl+C to quit)"
        & $Py "$D\laptop\drive.py" @Rest
        exit $LASTEXITCODE
    }
    "12-*" {
        # "run.bat channels", "run.bat add VIDEO", "run.bat list"...: only the laptop side, without flashing
        if ($Rest.Count -gt 0) { Run-Laptop (@("$D\laptop\tv.py") + $Rest) }
        Flash-Robot "$D\firmware"
        Say "Looking for the robot on the Wi-Fi to send it the videos (Ctrl+C to quit)"
        Run-Laptop @("$D\laptop\tv.py")
    }
    default { Fail "Demo $D doesn't have a run.bat yet" }
}
