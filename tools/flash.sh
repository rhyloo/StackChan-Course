#!/usr/bin/env bash
# Builds and flashes a firmware to the board connected over USB and opens the serial monitor.
#     ./tools/flash.sh demos/01-vr-cardboard/firmware
#     ./tools/flash.sh demos/03-espnow-beacons/beacon --env beacon2 /dev/ttyUSB0
#     ./tools/flash.sh demos/01-vr-cardboard/firmware --no-monitor
# With no arguments it lists the available firmwares.
# If the CoreS3 is not detected: hold its RST button ~3 s until the LED turns
# green (download mode) and try again.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Run ./setup.sh first"; exit 1; }
[ -f config/wifi_secrets.h ] || { echo "The Wi-Fi settings are missing: ./setup.sh --wifi"; exit 1; }
if [ $# -eq 0 ]; then
    echo "Usage: ./tools/flash.sh <firmware-folder> [port] [--env ENV] [--no-monitor]"
    echo "Available firmwares:"
    find demos -name platformio.ini -not -path '*/.pio/*' -printf '  %h\n' | sort
    exit 1
fi
PROJECT="${1%/}"; shift
[ -f "$PROJECT/platformio.ini" ] || { echo "There is no platformio.ini in $PROJECT"; exit 1; }
MONITOR=1
UPLOAD=()
MON=()
ENVS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --no-monitor) MONITOR=0 ;;
        --env) shift; ENVS=(-e "$1"); MON+=(-e "$1") ;;
        *) UPLOAD+=(--upload-port "$1"); MON+=(--port "$1") ;;
    esac
    shift
done
$PIO run --project-dir "$PROJECT" ${ENVS[@]+"${ENVS[@]}"} -t upload ${UPLOAD[@]+"${UPLOAD[@]}"}
if [ "$MONITOR" = 1 ]; then
    echo "Serial monitor (Ctrl+C to quit)."
    exec $PIO device monitor --project-dir "$PROJECT" ${MON[@]+"${MON[@]}"}
fi
