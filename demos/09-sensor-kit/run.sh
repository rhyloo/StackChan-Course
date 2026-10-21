#!/usr/bin/env bash
# Demo 09 in one step: flash the kit module reader to the robot and open the serial monitor.
#     demos/09-sensor-kit/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 09 "$@"
