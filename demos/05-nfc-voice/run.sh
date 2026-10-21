#!/usr/bin/env bash
# Demo 05 in one step: flash the firmware to the robot and start the Gemini relay.
#     demos/05-nfc-voice/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [options]
exec "$(dirname "$0")/../../tools/run_demo.sh" 05 "$@"
