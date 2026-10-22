#!/usr/bin/env bash
# Demo 13 in one step: flash the web server to the robot and show its address (open it in a browser).
#     demos/13-web-server/run.sh [--flash-only | --debug | --no-flash] [--usb PORT]
exec "$(dirname "$0")/../../tools/run_demo.sh" 13 "$@"
