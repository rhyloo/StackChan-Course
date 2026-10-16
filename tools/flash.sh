#!/usr/bin/env bash
# Compila y flashea el firmware en el robot conectado por USB, y abre el monitor serie.
#     ./tools/flash.sh              autodetecta el puerto
#     ./tools/flash.sh /dev/ttyACM0
#     ./tools/flash.sh --no-monitor
# Si no lo detecta: mantén pulsado el botón de reset del CoreS3 ~3 s hasta que
# se encienda el LED verde (modo descarga) y vuelve a intentarlo.
set -euo pipefail
cd "$(dirname "$0")/.."
PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Primero ejecuta ./setup.sh"; exit 1; }
[ -f firmware/include/wifi_secrets.h ] || { echo "Falta la Wi-Fi: ./setup.sh --wifi"; exit 1; }
MONITOR=1
UPLOAD=()
MON=()
for arg in "$@"; do
    case "$arg" in
        --no-monitor) MONITOR=0 ;;
        *) UPLOAD=(--upload-port "$arg"); MON=(--port "$arg") ;;
    esac
done
$PIO run --project-dir firmware -t upload ${UPLOAD[@]+"${UPLOAD[@]}"}
if [ "$MONITOR" = 1 ]; then
    echo "Monitor serie (Ctrl+C para salir). Escribe 'info' + Enter para ver el estado."
    exec $PIO device monitor --project-dir firmware ${MON[@]+"${MON[@]}"}
fi
