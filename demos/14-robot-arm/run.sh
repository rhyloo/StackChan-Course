#!/usr/bin/env bash
# Demo 14 in one step: build the micro:bit program, copy it to the MICROBIT drive and open the arm's web page.
#     demos/14-robot-arm/run.sh [--flash-only | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 14 "$@"
