#!/usr/bin/env bash
# Demo 02 in one step: flash the firmware to the robot and start the Gemini relay.
#     demos/02-gemini-voice/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [options]
exec "$(dirname "$0")/../../tools/run_demo.sh" 02 "$@"
