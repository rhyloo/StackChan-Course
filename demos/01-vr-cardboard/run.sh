#!/usr/bin/env bash
# Demo 01 en un paso: sube el firmware al robot y arranca la pasarela del móvil.
#     demos/01-vr-cardboard/run.sh [--sin-flashear] [--puerto PUERTO] [opciones]
exec "$(dirname "$0")/../../tools/run_demo.sh" 01 "$@"
