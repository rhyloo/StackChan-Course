#!/usr/bin/env bash
# Demo 04 in one step: flash the NFC reader to the robot and show what it reads.
#     demos/04-nfc/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [options]
exec "$(dirname "$0")/../../tools/run_demo.sh" 04 "$@"
