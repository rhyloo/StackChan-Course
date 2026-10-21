#!/usr/bin/env bash
# Demo 06 in one step: flash the infrared firmware to the robot and show the codes it reads.
#     demos/06-infrared/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 06 "$@"
