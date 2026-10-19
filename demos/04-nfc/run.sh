#!/usr/bin/env bash
# Demo 04 en un paso: sube el lector NFC al robot y muestra lo que lee.
#     demos/04-nfc/run.sh [--sin-flashear] [--puerto PUERTO] [opciones]
exec "$(dirname "$0")/../../tools/run_demo.sh" 04 "$@"
