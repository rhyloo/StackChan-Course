#!/usr/bin/env bash
# Demo 02 en un paso: sube el firmware al robot y arranca la pasarela con Gemini.
#     demos/02-voz-gemini/run.sh [--sin-flashear] [--puerto PUERTO] [opciones]
exec "$(dirname "$0")/../../tools/run_demo.sh" 02 "$@"
