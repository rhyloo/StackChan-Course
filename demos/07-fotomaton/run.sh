#!/usr/bin/env bash
# Demo 07 en un paso: sube el fotomatón al robot y arranca la pasarela con los filtros.
#     demos/07-fotomaton/run.sh [--sin-flashear] [--puerto PUERTO]
exec "$(dirname "$0")/../../tools/run_demo.sh" 07 "$@"
