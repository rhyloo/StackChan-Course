#!/usr/bin/env bash
# Demo 05 en un paso: sube el firmware al robot y arranca la pasarela con Gemini.
#     demos/05-nfc-voz/run.sh [--sin-flashear] [--puerto PUERTO] [opciones]
exec "$(dirname "$0")/../../tools/run_demo.sh" 05 "$@"
