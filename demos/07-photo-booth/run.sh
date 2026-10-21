#!/usr/bin/env bash
# Demo 07 in one step: flash the photo booth to the robot and start the relay with the filters.
#     demos/07-photo-booth/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 07 "$@"
