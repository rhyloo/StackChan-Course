#!/usr/bin/env bash
# Lo que hacen los run.sh de cada demo: comprueba que todo está listo, sube el
# firmware al robot y arranca lo que la demo necesita en el portátil.
#
#     tools/run_demo.sh <demo> [--sin-flashear] [--puerto /dev/ttyACM0] [opciones de la pasarela]
#     tools/run_demo.sh 03 balizas      # demo 03: flashea las tres balizas, una detrás de otra
#
# <demo> es el número (01, 02...) o la carpeta. Lo normal es no llamarlo a mano
# sino con demos/<demo>/run.sh o con ./demo.sh (menú).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PY=.venv/bin/python

say()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[1;31m%s\033[0m\n' "$*" >&2; exit 1; }

[ $# -ge 1 ] || fail "Uso: tools/run_demo.sh <demo> [--sin-flashear] [--puerto PUERTO] [opciones]"
DEMO="$(ls -d demos/"${1#demos/}"* 2>/dev/null | head -1 || true)"
DEMO="${DEMO%/}"
[ -d "$DEMO" ] || fail "No existe la demo '$1'. Hay: $(ls demos | tr '\n' ' ')"
shift

[ -x "$PY" ] || fail "Falta el entorno. Ejecuta primero: ./setup.sh"
[ -f config/wifi_secrets.h ] || fail "Falta la Wi-Fi. Ejecuta: ./setup.sh --wifi"

FLASH=1
PORT=""
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --sin-flashear) FLASH=0 ;;
        --puerto) shift; PORT="$1" ;;
        *) ARGS+=("$1") ;;
    esac
    shift
done

need_gemini_key() {
    [ -n "${GEMINI_API_KEY:-}" ] || fail "Esta demo usa Gemini y falta la clave.
Créala gratis en https://aistudio.google.com/apikey y ejecuta:
    export GEMINI_API_KEY=\"tu-clave\"
(para no repetirlo, añade esa línea al final de ~/.bashrc)"
}

# flash_robot <carpeta-del-firmware>
flash_robot() {
    [ "$FLASH" = 1 ] || { say "Sin flashear (--sin-flashear): uso lo que ya tiene el robot"; return; }
    local port="$PORT"
    [ -n "$port" ] || port="$($PY tools/find_port.py)"
    [ -n "$port" ] || fail "No encuentro el robot por USB.
- Conéctalo con un cable USB-C de datos (algunos solo cargan).
- Si sigue sin aparecer, mantén pulsado su botón RST unos 3 s, hasta que el LED se ponga verde, y repite."
    say "Compilando y subiendo $1 al robot ($port). La primera vez tarda unos minutos..."
    ./tools/flash.sh "$1" "$port" --no-monitor || fail "No se pudo subir el firmware.
- ¿Tienes abierto un monitor serie? Ciérralo: ocupa el puerto.
- Si no, mantén pulsado RST unos 3 s (LED verde, modo descarga) y repite."
    say "Firmware subido. El robot se reinicia (unos segundos)."
}

relay() {
    say "Arrancando la pasarela del portátil (Ctrl+C para salir)"
    exec "$PY" "$DEMO/relay/main.py" ${ARGS[@]+"${ARGS[@]}"}
}

case "$(basename "$DEMO")" in
    01-*)
        flash_robot "$DEMO/firmware"
        relay ;;
    02-*|05-*)
        need_gemini_key
        flash_robot "$DEMO/firmware"
        relay ;;
    03-*)
        if [ "${ARGS[0]:-}" = balizas ]; then
            for n in 1 2 3; do
                read -r -p "Conecta SOLO la baliza $n (ESP32 DevKit) por USB y pulsa Enter... " _
                port="${PORT:-$($PY tools/find_port.py --devkit)}"
                [ -n "$port" ] || fail "No veo ninguna ESP32 DevKit (chip CP210x o CH34x). ¿Cable de datos? ¿Driver?"
                say "Subiendo la baliza $n ($port)"
                ./tools/flash.sh "$DEMO/baliza" --env "baliza$n" "$port" --no-monitor
            done
            say "Balizas listas. Ahora: $DEMO/run.sh"
            exit 0
        fi
        flash_robot "$DEMO/receptor"
        say "Abriendo el mapa (Ctrl+C para salir). Para calibrar: $PY $DEMO/portatil/posicion.py --calibrar 1"
        exec "$PY" "$DEMO/portatil/posicion.py" ${ARGS[@]+"${ARGS[@]}"} ;;
    04-*)
        flash_robot "$DEMO/firmware"
        say "Acerca una tarjeta NFC a la base del robot. Aquí verás lo que lee (Ctrl+C para salir)."
        exec "$PY" tools/serial_log.py "$DEMO/firmware" ${PORT:+"$PORT"} ;;
    08-*)
        flash_robot "$DEMO/firmware"
        say "Mueve el servo desde la pantalla del robot. Aquí verás lo que dice el robot (Ctrl+C para salir)."
        exec "$PY" tools/serial_log.py "$DEMO/firmware" ${PORT:+"$PORT"} ;;
    07-*)
        [ -n "${GEMINI_API_KEY:-}" ] || say "Sin GEMINI_API_KEY solo funcionan Original, Boceto y Píxel (los filtros con IA usan Gemini)."
        flash_robot "$DEMO/firmware"
        relay ;;
    06-*)
        flash_robot "$DEMO/firmware"
        say "Apunta un mando al robot y pulsa botones. Aquí verás los códigos (Ctrl+C para salir)."
        exec "$PY" tools/serial_log.py "$DEMO/firmware" ${PORT:+"$PORT"} ;;
    *)
        fail "La demo $DEMO todavía no tiene run.sh" ;;
esac
