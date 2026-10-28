#!/usr/bin/env bash
# Demo 16 in one step: flash the math game to the robot and show its serial output.
#     demos/16-math-game/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 16 "$@"
