#!/usr/bin/env python3
"""Demo 02.1 - Head and moods by voice: the laptop relay.

    export GEMINI_API_KEY=...             # https://aistudio.google.com/apikey
    demos/02.1-voice-moves/run.sh         # flashes the robot and starts this

It is the demo 02 relay (voice with Gemini Live) with the functions in
voice_moves.py. No VR viewer and no video: the head only moves by voice.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "02-gemini-voice", "relay"))
sys.path.insert(0, HERE)

import hooks  # noqa: E402
import processing  # noqa: E402
import voice_moves  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 02.1: move the head, show moods and dance by voice (Gemini Live)")
    g = parser.add_argument_group("voice")
    g.add_argument("--model", default="gemini-3.1-flash-live-preview", help="Gemini Live model")
    g.add_argument("--voice", default="Puck", help="Gemini voice: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--push-to-talk", action="store_true",
                   help="push to talk (screen tap or Enter) instead of hands-free")
    parser.set_defaults(no_vision=True)   # no video in this demo
    main(hooks, processing, parser=parser, setup=voice_moves.setup)
