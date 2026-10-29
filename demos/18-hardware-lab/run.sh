#!/usr/bin/env bash
# Demo 18 in one step: flash the hardware lab to the robot and show its serial output.
#     demos/18-hardware-lab/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 18 "$@"
