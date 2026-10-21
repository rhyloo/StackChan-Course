#!/usr/bin/env bash
# Demo 02.1 in one step: flash the firmware to the robot and start the Gemini relay (head, moods and dance by voice).
#     demos/02.1-voice-moves/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [options]
exec "$(dirname "$0")/../../tools/run_demo.sh" 02.1 "$@"
