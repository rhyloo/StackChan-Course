#!/usr/bin/env bash
# Pairs a PS4 gamepad (DualShock 4) with this laptop over Bluetooth (Linux).
#     tools/pair_gamepad.sh
# Before running it, put the gamepad in pairing mode: hold SHARE and PS
# together for about 3 s, until the light bar blinks fast (double flashes).
# You only need it once: after that, pressing PS is enough for it to connect.
# On Windows or macOS, pair it from the system's Bluetooth settings.
set -euo pipefail

say()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[1;31m%s\033[0m\n' "$*" >&2; exit 1; }

command -v bluetoothctl >/dev/null || fail "bluetoothctl is missing: sudo apt install bluez"
systemctl is-active --quiet bluetooth || fail "The Bluetooth service is off. Start it with:
    sudo systemctl start bluetooth"

bluetoothctl power on >/dev/null
bluetoothctl pairable on >/dev/null

# Already paired? It only needs connecting.
mac="$(bluetoothctl devices 2>/dev/null | awk '/Wireless Controller/ {print $2; exit}')"
if [ -n "$mac" ] && bluetoothctl info "$mac" 2>/dev/null | grep -q "Paired: yes"; then
    say "The gamepad ($mac) is already paired. Press PS on the gamepad to connect it."
    bluetoothctl connect "$mac" >/dev/null 2>&1 || true
    exit 0
fi

say "Looking for the gamepad for 20 s. Hold SHARE + PS until the light blinks fast..."
mac="$(timeout 25 bluetoothctl --timeout 20 scan on 2>/dev/null | awk '/Wireless Controller/ {print $3; exit}' || true)"
[ -n "$mac" ] || fail "I didn't find it. Try again with the light blinking fast (SHARE + PS 3 s)."

say "Found: $mac. Pairing..."
bluetoothctl pair "$mac" >/dev/null || fail "Could not pair. Put it in pairing mode again and retry."
bluetoothctl trust "$mac" >/dev/null
bluetoothctl connect "$mac" >/dev/null || true
say "Gamepad paired. The light stays solid when it is connected. Now: demos/10-voice-gamepad/run.sh"
