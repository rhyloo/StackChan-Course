#!/usr/bin/env bash
# Demo 08 en un paso: sube el control del servo SG90 al robot y abre el monitor serie.
#     demos/08-servo-sg90/run.sh [--sin-flashear] [--puerto PUERTO]
exec "$(dirname "$0")/../../tools/run_demo.sh" 08 "$@"
