#!/usr/bin/env bash
# Demo 15 in one step: flash the OTA base over USB once; then send tests over the Wi-Fi.
#     demos/15-ota/run.sh [--flash-only | --no-flash] [--usb PORT]   # first time: USB
#     demos/15-ota/run.sh send leds | find | tests | log [--host NAME]  # then: Wi-Fi
exec "$(dirname "$0")/../../tools/run_demo.sh" 15 "$@"
