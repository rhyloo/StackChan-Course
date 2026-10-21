#!/usr/bin/env bash
# Demo 10 in one step: flash demo 02's firmware to the robot and start the relay with voice and PS4 gamepad.
#     demos/10-voice-gamepad/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [relay options]
# Pair the gamepad once with tools/pair_gamepad.sh.
exec "$(dirname "$0")/../../tools/run_demo.sh" 10 "$@"
