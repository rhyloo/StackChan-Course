#!/usr/bin/env bash
# Demo 08 in one step: flash the SG90 servo control to the robot and open the serial monitor.
#     demos/08-servo-sg90/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 08 "$@"
