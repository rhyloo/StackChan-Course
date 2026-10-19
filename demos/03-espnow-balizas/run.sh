#!/usr/bin/env bash
# Demo 03 en un paso: sube el receptor al robot y abre el mapa. 'run.sh balizas' flashea las tres balizas.
#     demos/03-espnow-balizas/run.sh [--sin-flashear] [--puerto PUERTO] [opciones]
exec "$(dirname "$0")/../../tools/run_demo.sh" 03 "$@"
