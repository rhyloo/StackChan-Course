#!/usr/bin/env bash
# Demo 17 in one step: flash the arcade to the robot and show its serial output.
#     demos/17-arcade/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 17 "$@"
