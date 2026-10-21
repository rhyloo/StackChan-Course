#!/usr/bin/env bash
# Demo 03 in one step: flash the receiver to the robot and open the map. 'run.sh beacons' flashes the three beacons.
#     demos/03-espnow-beacons/run.sh [--flash-only | --debug | --no-flash] [--usb PORT] [options]
exec "$(dirname "$0")/../../tools/run_demo.sh" 03 "$@"
