#!/usr/bin/env python3
"""Moves the robotic arm from the terminal, over the micro:bit's USB serial port.

    .venv/bin/python demos/14-robot-arm/laptop/console.py     # run.sh opens the web page instead

Type a command and press Enter (the full list is at the top of microbit/main.ts):
    run 1 50           M1 at speed 50 for half a second
    move 2 -90         M2 90 degrees the other way
    goto 1 0           M1 back to 0
    close / open       the gripper (c / o)
    stop               everything (s)

What the micro:bit answers is shown and saved to logs/microbit-*.log.
"""
import argparse
import sys
import time

from microbit_link import MicroBit

SHORT = {"c": "close", "o": "open", "s": "stop"}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="the micro:bit's serial port (default: found automatically)")
    args = ap.parse_args()

    mb = MicroBit(args.port, on_line=lambda text: print(time.strftime("%H:%M:%S ") + text), echo=False)
    print(f"Saving to {mb.log_path}.")
    print("Commands: run, move, goto, stop (s), zero, speed, pos, close (c), open (o). Ctrl+C to quit.")
    try:
        for line in sys.stdin:
            words = line.strip().split()
            if words:
                words[0] = SHORT.get(words[0], words[0])
                mb.send(" ".join(words))
    except KeyboardInterrupt:
        pass
    mb.send("stop")
    time.sleep(0.2)
    print()


if __name__ == "__main__":
    main()
