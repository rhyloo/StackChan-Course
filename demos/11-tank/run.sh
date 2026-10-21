#!/usr/bin/env bash
# Demo 11 in one step: drive the Keyestudio tank with the PS4 gamepad.
#     demos/11-tank/run.sh            # drive (the gamepad paired with the laptop)
#     demos/11-tank/run.sh tank       # once: flash the firmware to the tank's Arduino
#     demos/11-tank/run.sh --tank BT05   # if there are several Bluetooth modules nearby
exec "$(dirname "$0")/../../tools/run_demo.sh" 11 "$@"
