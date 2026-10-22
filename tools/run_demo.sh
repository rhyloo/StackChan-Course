#!/usr/bin/env bash
# What each demo's run.sh does: checks that everything is ready, flashes the
# firmware to the robot and starts what the demo needs on the laptop.
#
#     tools/run_demo.sh <demo> [mode] [--usb /dev/ttyACM0] [relay options]
#
# Modes:
#     (none)           flash the firmware and start the demo
#     --flash-only     only build and flash the firmware; it stays in the robot's flash
#     --debug          like no mode, but save the robot's serial (logs/robot-*.log)
#                      and the relay with full detail (logs/relay-*.log)
#     --no-flash       start the demo with what the robot already has
#
#     tools/run_demo.sh 03 beacons      # demo 03: flash the three beacons, one after another
#
# <demo> is the number (01, 02...) or the folder. You normally don't call it by
# hand but through demos/<demo>/run.sh or ./demo.sh (menu).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PY=.venv/bin/python

say()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[1;31m%s\033[0m\n' "$*" >&2; exit 1; }

[ $# -ge 1 ] || fail "Usage: tools/run_demo.sh <demo> [--flash-only | --debug | --no-flash] [--usb PORT] [options]"
# "02" must be demo 02, not 02.1: try the exact number first.
DEMO="$(ls -d demos/"${1#demos/}"-* 2>/dev/null | head -1 || true)"
[ -n "$DEMO" ] || DEMO="$(ls -d demos/"${1#demos/}"* 2>/dev/null | head -1 || true)"
DEMO="${DEMO%/}"
[ -d "$DEMO" ] || fail "Demo '$1' does not exist. Available: $(ls demos | tr '\n' ' ')"
shift

# --help / -h: the modes, then the options of the program the demo runs on the laptop.
show_help() {
    local laptop=""
    case "$(basename "$DEMO")" in
        01-*|02-*|02.1-*|05-*|07-*|10-*) laptop="$DEMO/relay/main.py" ;;
        03-*) laptop="$DEMO/laptop/position.py" ;;
        11-*) laptop="$DEMO/laptop/drive.py" ;;
        12-*) laptop="$DEMO/laptop/tv.py" ;;
        14-*) laptop="$DEMO/laptop/arm_web.py" ;;
    esac
    cat <<EOF
Usage: $DEMO/run.sh [mode] [--usb PORT] [options]

Modes:
  (none)        flash the firmware and start the demo
  --flash-only  only build and flash the firmware; it stays on the robot
  --debug       like no mode, and save logs/robot-*.log (robot serial) and logs/relay-*.log
  --no-flash    start the demo with what the robot already has
  --usb PORT    USB port of the board (default: found automatically)
  -h, --help    show this help
EOF
    case "$(basename "$DEMO")" in
        03-*) printf '\nAlso:\n  %s/run.sh beacons   flash the three ESP32 beacons, one after another\n' "$DEMO" ;;
        12-*) printf '\nAlso, without flashing:\n  %s/run.sh channels | add VIDEO | list | remove NAME\n' "$DEMO" ;;
        11-*) printf '\nAlso:\n  %s/run.sh tank      flash the tank'"'"'s Arduino (remove the HM-10 first)\nThe robot (optional, for the L2/R2 sounds) gets demo 02'"'"'s firmware; --debug does nothing here.\n' "$DEMO" ;;
    esac
    if [ -z "$laptop" ]; then
        printf '\nThis demo has nothing to run on the laptop: it shows the robot'"'"'s serial output.\n'
    elif [ -x "$PY" ]; then
        printf '\nAny other option goes to %s:\n\n' "$laptop"
        "$PY" "$laptop" --help
    fi
    exit 0
}
for a in "$@"; do case "$a" in -h|--help) show_help ;; esac; done

[ -x "$PY" ] || fail "The environment is missing. Run first: ./setup.sh"
[ -f config/wifi_secrets.h ] || fail "The Wi-Fi settings are missing. Run: ./setup.sh --wifi"

MODE=normal     # normal | flash-only | debug
FLASH=1
PORT=""
FW=""           # firmware flashed on the robot (for serial_log.py to decode crashes)
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --flash-only) MODE=flash-only ;;
        --debug) MODE=debug ;;
        --no-flash) FLASH=0 ;;
        --usb) shift; PORT="$1" ;;
        *) ARGS+=("$1") ;;
    esac
    shift
done
[ "$MODE" != flash-only ] || [ "$FLASH" = 1 ] || fail "--flash-only and --no-flash don't go together."

need_gemini_key() {
    [ "$MODE" != flash-only ] || return 0
    [ -n "${GEMINI_API_KEY:-}" ] || fail "This demo uses Gemini and the key is missing.
Create one for free at https://aistudio.google.com/apikey and run:
    export GEMINI_API_KEY=\"your-key\"
(to avoid repeating it, add that line at the end of ~/.bashrc)"
}

# flash_robot <firmware-folder>. With --flash-only it stops here.
flash_robot() {
    FW="$1"
    [ "$FLASH" = 1 ] || { say "Not flashing (--no-flash): using what the robot already has"; return; }
    local port="$PORT"
    [ -n "$port" ] || port="$($PY tools/find_port.py)"
    [ -n "$port" ] || fail "I can't find the robot on USB.
- Connect it with a USB-C data cable (some cables only charge).
- If it still doesn't show up, hold its RST button for about 3 s, until the LED turns green, and try again."
    say "Building and flashing $1 to the robot ($port). The first time takes a few minutes..."
    ./tools/flash.sh "$1" "$port" --no-monitor || fail "Could not flash the firmware.
- Do you have a serial monitor open? Close it: it holds the port.
- If not, hold RST for about 3 s (green LED, download mode) and try again."
    if [ "$MODE" = flash-only ]; then
        say "Firmware flashed. It stays on the robot even if you unplug it or switch it off."
        [ ! -d "$DEMO/relay" ] || echo "This demo needs the laptop relay: $DEMO/run.sh --no-flash"
        exit 0
    fi
    say "Firmware flashed. The robot restarts (a few seconds)."
}

# run <command...>: starts the laptop side. With --debug the robot's serial goes
# to logs/robot-*.log in the background while it runs.
run() {
    if [ "$MODE" != debug ] || [ -z "$FW" ]; then exec "$@"; fi
    "$PY" tools/serial_log.py "$FW" ${PORT:+"$PORT"} >/dev/null 2>&1 &
    local pid=$!
    trap "kill $pid 2>/dev/null" EXIT
    say "Debugging: the robot's serial goes to logs/robot-*.log"
    "$@"
}

relay() {
    say "Starting the laptop relay (Ctrl+C to quit)"
    local extra=()
    [ "$MODE" != debug ] || extra=(--debug)
    run "$PY" "$DEMO/relay/main.py" ${extra[@]+"${extra[@]}"} ${ARGS[@]+"${ARGS[@]}"}
}

# Demos without a relay: the serial monitor already saves to logs/robot-*.log.
monitor() {
    say "$1 Here you'll see what the robot says; it is saved to logs/robot-*.log (Ctrl+C to quit)."
    exec "$PY" tools/serial_log.py "$FW" ${PORT:+"$PORT"}
}

case "$(basename "$DEMO")" in
    01-*)
        flash_robot "$DEMO/firmware"
        relay ;;
    02-*|02.1-*|05-*)
        need_gemini_key
        flash_robot "$DEMO/firmware"
        relay ;;
    03-*)
        if [ "${ARGS[0]:-}" = beacons ]; then
            for n in 1 2 3; do
                read -r -p "Connect ONLY beacon $n (ESP32 DevKit) over USB and press Enter... " _
                port="${PORT:-$($PY tools/find_port.py --devkit)}"
                [ -n "$port" ] || fail "I can't see any ESP32 DevKit (CP210x or CH34x chip). Data cable? Driver?"
                say "Flashing beacon $n ($port)"
                ./tools/flash.sh "$DEMO/beacon" --env "beacon$n" "$port" --no-monitor
            done
            say "Beacons ready. Now: $DEMO/run.sh"
            exit 0
        fi
        flash_robot "$DEMO/receiver"
        say "Opening the map (Ctrl+C to quit). To calibrate: $PY $DEMO/laptop/position.py --calibrate 1"
        run "$PY" "$DEMO/laptop/position.py" ${ARGS[@]+"${ARGS[@]}"} ;;
    04-*)
        flash_robot "$DEMO/firmware"
        monitor "Hold an NFC card near the robot's base." ;;
    06-*)
        flash_robot "$DEMO/firmware"
        monitor "Point a remote control at the robot and press buttons." ;;
    07-*)
        [ -n "${GEMINI_API_KEY:-}" ] || [ "$MODE" = flash-only ] || say "Without GEMINI_API_KEY only Original, Sketch and Pixel work (the AI filters use Gemini)."
        flash_robot "$DEMO/firmware"
        relay ;;
    08-*)
        flash_robot "$DEMO/firmware"
        monitor "Move the servo from the robot's screen." ;;
    09-*)
        flash_robot "$DEMO/firmware"
        monitor "Choose the module type on the robot's screen." ;;
    13-*)
        flash_robot "$DEMO/firmware"
        monitor "Open the address it prints (or tap the robot's screen for a QR code) on a phone or laptop on the same Wi-Fi." ;;
    10-*)   # the robot runs demo 02's firmware; only the laptop side changes
        need_gemini_key
        flash_robot "demos/02-gemini-voice/firmware"
        relay ;;
    11-*)   # "run.sh tank": the tank's Arduino. Otherwise the robot (optional: the L2/R2 sounds) gets demo 02's firmware.
        if [ "${ARGS[0]:-}" = tank ]; then
            echo
            read -r -p "REMOVE the Bluetooth module (HM-10) from the tank, connect the Arduino over USB and press Enter... " _
            port="${PORT:-$($PY tools/find_port.py --arduino)}"
            [ -n "$port" ] || fail "I can't see the tank's Arduino on USB. Data cable? Board powered?"
            say "Flashing the firmware to the tank ($port)"
            ./tools/flash.sh "$DEMO/tank" "$port" --no-monitor
            say "Done. Put the Bluetooth module back and switch the tank on. Then: $DEMO/run.sh"
            exit 0
        fi
        if [ "$FLASH" = 1 ] && [ -z "$PORT" ] && [ -z "$($PY tools/find_port.py)" ]; then
            [ "$MODE" != flash-only ] || fail "I can't find the robot on USB."
            say "No robot on USB: driving without it. The L2/R2 sounds need it with demo 02's firmware."
            FLASH=0
        fi
        [ "$FLASH" = 0 ] || flash_robot "demos/02-gemini-voice/firmware"
        say "Drive with the PS4 gamepad; L2 horn, R2 blaster on the robot (Ctrl+C to quit)"
        exec "$PY" "$DEMO/laptop/drive.py" ${ARGS[@]+"${ARGS[@]}"} ;;
    14-*)   # a micro:bit, not the robot: MakeCode's compiler builds it and the .hex is copied to the MICROBIT drive
        if [ "$FLASH" = 1 ]; then
            say "Building $DEMO/microbit and copying it to the micro:bit. The first time installs the MakeCode compiler..."
            "$PY" tools/microbit.py flash "$DEMO/microbit" || fail "Could not flash the micro:bit."
            [ "$MODE" != flash-only ] || exit 0
        fi
        say "Opening the arm's web page (Ctrl+C to quit). The micro:bit's A and B buttons still move the gripper."
        run "$PY" "$DEMO/laptop/arm_web.py" ${PORT:+--port "$PORT"} ${ARGS[@]+"${ARGS[@]}"} ;;
    12-*)   # "run.sh channels", "run.sh add VIDEO", "run.sh list"...: only the laptop side, without flashing
        if [ ${#ARGS[@]} -gt 0 ]; then run "$PY" "$DEMO/laptop/tv.py" "${ARGS[@]}"; fi
        flash_robot "$DEMO/firmware"
        say "Looking for the robot on the Wi-Fi to send it the videos (Ctrl+C to quit)"
        run "$PY" "$DEMO/laptop/tv.py" ;;
    *)
        fail "Demo $DEMO doesn't have a run.sh yet" ;;
esac
