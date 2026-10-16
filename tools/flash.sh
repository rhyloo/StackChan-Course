#!/usr/bin/env bash
# Compila y flashea un firmware en la placa conectada por USB y abre el monitor serie.
#     ./tools/flash.sh demos/01-vr-cardboard/firmware
#     ./tools/flash.sh demos/03-espnow-balizas/baliza --env baliza2 /dev/ttyUSB0
#     ./tools/flash.sh demos/01-vr-cardboard/firmware --no-monitor
# Sin argumentos lista los firmwares que hay.
# Si no detecta el CoreS3: mantén pulsado su botón RST ~3 s hasta que el LED se
# ponga verde (modo descarga) y vuelve a intentarlo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Primero ejecuta ./setup.sh"; exit 1; }
[ -f config/wifi_secrets.h ] || { echo "Falta la Wi-Fi: ./setup.sh --wifi"; exit 1; }
if [ $# -eq 0 ]; then
    echo "Uso: ./tools/flash.sh <carpeta-del-firmware> [puerto] [--no-monitor]"
    echo "Firmwares disponibles:"
    find demos -name platformio.ini -not -path '*/.pio/*' -printf '  %h\n' | sort
    exit 1
fi
PROJECT="${1%/}"; shift
[ -f "$PROJECT/platformio.ini" ] || { echo "No hay platformio.ini en $PROJECT"; exit 1; }
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
    echo "Monitor serie (Ctrl+C para salir)."
    exec $PIO device monitor --project-dir "$PROJECT" ${MON[@]+"${MON[@]}"}
fi
