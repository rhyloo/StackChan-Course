#!/usr/bin/env bash
# Demo 06 en un paso: sube el firmware de infrarrojos al robot y muestra los códigos que lee.
#     demos/06-infrarrojos/run.sh [--sin-flashear] [--puerto PUERTO]
exec "$(dirname "$0")/../../tools/run_demo.sh" 06 "$@"
