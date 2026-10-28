#!/usr/bin/env python3
"""Demo 11 with Stack-chan on top: VR viewer + voice (demo 02) and the tank driven with the PS4 gamepad.

    export GEMINI_API_KEY=...        # https://aistudio.google.com/apikey
    tools/pair_gamepad.sh            # once: pairs the gamepad with the laptop
    demos/11-tank/run.sh vr          # flashes the robot (demo 02 firmware) and starts this

It is the demo 02 relay (voice.py, hooks.py and processing.py from there) plus
tank_pad.py: the phone moves the head, the gamepad moves the tank.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "02-gemini-voice", "relay"))
sys.path.insert(0, HERE)

import hooks  # noqa: E402
import processing  # noqa: E402
import tank_pad  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 11: Stack-chan on the tank, with the VR viewer, voice (Gemini Live) and the PS4 gamepad")
    g = parser.add_argument_group("voice")
    g.add_argument("--model", default="gemini-3.1-flash-live-preview", help="Gemini Live model")
    g.add_argument("--voice", default="Puck", help="Gemini voice: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--no-vision", action="store_true", help="don't send camera images to Gemini")
    g.add_argument("--push-to-talk", action="store_true",
                   help="push to talk (L1 on the gamepad, screen tap or Enter) instead of hands-free")
    g = parser.add_argument_group("tank")
    g.add_argument("--tank", help="Bluetooth name or MAC of the tank's HM-10 module")
    main(hooks, processing, parser=parser, setup=tank_pad.setup)
    if tank_pad._tank:
        tank_pad._tank.stop()
        time.sleep(0.3)        # let the last stop command go out
