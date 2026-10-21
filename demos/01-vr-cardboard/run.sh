#!/usr/bin/env bash
# Demo 01 in one step: flash the firmware to the robot and start the phone relay.
#     demos/01-vr-cardboard/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [options]
exec "$(dirname "$0")/../../tools/run_demo.sh" 01 "$@"
