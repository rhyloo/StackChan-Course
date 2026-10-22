#!/usr/bin/env bash
# Demo 12 in one step: flash the mini TV to the robot and send it videos over Wi-Fi.
#     demos/12-mini-tv/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
#     demos/12-mini-tv/run.sh channels | add VIDEO | list | remove NAME     (only the laptop, without flashing)
exec "$(dirname "$0")/../../tools/run_demo.sh" 12 "$@"
