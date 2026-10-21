#!/usr/bin/env python3
"""Demo 10 - Voice with Gemini and the head moved with a PS4 gamepad paired with the laptop.

    export GEMINI_API_KEY=...        # https://aistudio.google.com/apikey
    tools/pair_gamepad.sh         # once: pairs the gamepad with the laptop
    demos/10-voice-gamepad/run.sh        # flashes the robot (demo 02 firmware) and starts this

It is the demo 02 relay (voice.py, hooks.py and processing.py from there) plus
gamepad.py, which reads the gamepad and moves the head.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "02-gemini-voice", "relay"))
sys.path.insert(0, HERE)

import gamepad  # noqa: E402
import hooks  # noqa: E402
import processing  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 10: Stack-chan with voice (Gemini Live) and a PS4 gamepad on the laptop")
    g = parser.add_argument_group("voice")
    g.add_argument("--model", default="gemini-3.1-flash-live-preview", help="Gemini Live model")
    g.add_argument("--voice", default="Puck", help="Gemini voice: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--no-vision", action="store_true", help="don't send camera images to Gemini")
    g.add_argument("--push-to-talk", action="store_true",
                   help="push to talk (L1 on the gamepad, screen tap or Enter) instead of hands-free")
    main(hooks, processing, parser=parser, setup=gamepad.setup)
