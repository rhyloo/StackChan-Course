#!/usr/bin/env python3
"""Drive the Keyestudio tank with the PS4 gamepad (paired with the laptop).

    demos/11-tank/run.sh               # or: .venv/bin/python demos/11-tank/laptop/drive.py
    ... --tank BT05                    # if there are several Bluetooth modules nearby: its name or MAC

    left stick        drive: forward/back and turn (the more you tilt it, the faster)
    D-pad             forward, back and spin on the spot, at a fixed speed
    R1 (hold)         full speed
    ✕                 stop
"""
import argparse
import math
import os
import sys
import time

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
import pygame  # noqa: E402
from pygame._sdl2 import controller as sdl_gamepad  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tank import Tank  # noqa: E402

PERIOD = 0.04
DEAD_ZONE = 0.15
NORMAL = 0.6              # fraction of full speed without R1: easier to control
DPAD = {                  # (left, right) as a fraction of the speed
    pygame.CONTROLLER_BUTTON_DPAD_UP: (1, 1),
    pygame.CONTROLLER_BUTTON_DPAD_DOWN: (-1, -1),
    pygame.CONTROLLER_BUTTON_DPAD_LEFT: (-1, 1),
    pygame.CONTROLLER_BUTTON_DPAD_RIGHT: (1, -1),
}


def axis(value):
    v = value / 32767.0
    if abs(v) < DEAD_ZONE:
        return 0.0
    return math.copysign((abs(v) - DEAD_ZONE) / (1 - DEAD_ZONE), v)


def tracks(pad):
    """(left, right) from -1 to 1, from the gamepad."""
    for button, value in DPAD.items():
        if pad.get_button(button):
            return value
    forward = -axis(pad.get_axis(pygame.CONTROLLER_AXIS_LEFTY))   # stick up = forward
    turn = axis(pad.get_axis(pygame.CONTROLLER_AXIS_LEFTX))
    left, right = forward + turn, forward - turn                  # "arcade": a single stick
    biggest = max(1.0, abs(left), abs(right))
    return left / biggest, right / biggest


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tank", help="Bluetooth name or MAC of the tank's HM-10 module")
    args = ap.parse_args()

    tank = Tank(args.tank, log=lambda m: print(f"\n{m}"))
    pygame.init()
    sdl_gamepad.init()
    pad = None
    print("Looking for the tank and the gamepad... (Ctrl+C to quit)")
    try:
        while True:
            pygame.event.pump()
            if pad is not None and not pad.attached():
                print("\nGamepad disconnected")
                pad = None
                tank.stop()
            if pad is None:
                for i in range(sdl_gamepad.get_count()):
                    if sdl_gamepad.is_controller(i):
                        pad = sdl_gamepad.Controller(i)
                        print(f"\nGamepad connected: {pad.name}")
                        pad.rumble(0.4, 0.4, 200)
                if pad is None:
                    time.sleep(0.5)
                    continue
            if pad.get_button(pygame.CONTROLLER_BUTTON_A):
                left = right = 0.0
            else:
                left, right = tracks(pad)
            k = 255 * (1.0 if pad.get_button(pygame.CONTROLLER_BUTTON_RIGHTSHOULDER) else NORMAL)
            tank.move(left * k, right * k)
            status = tank.device if tank.connected else "looking for the tank..."
            print(f"\r  left {tank.left:+4d}  right {tank.right:+4d}   {status}      ", end="", flush=True)
            time.sleep(PERIOD)
    except KeyboardInterrupt:
        pass
    tank.stop()
    time.sleep(0.3)        # let the last stop command go out
    print("\nDone.")


if __name__ == "__main__":
    main()
