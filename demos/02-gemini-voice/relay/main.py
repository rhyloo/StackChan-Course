#!/usr/bin/env python3
"""Demo 02 - Voice with Gemini: the laptop relay.

    export GEMINI_API_KEY=...        # https://aistudio.google.com/apikey
    ./server.sh [--push-to-talk] [--model ...] [--voice Puck] [--no-vision]

It is the demo 01 relay plus voice.py (Gemini Live). The filters are in
processing.py and the buttons in hooks.py.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, HERE)

import hooks  # noqa: E402
import processing  # noqa: E402
import voice  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 02: Stack-chan with voice (Gemini Live)")
    g = parser.add_argument_group("voice")
    g.add_argument("--model", default="gemini-3.1-flash-live-preview", help="Gemini Live model")
    g.add_argument("--voice", default="Puck", help="Gemini voice: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--no-vision", action="store_true", help="don't send camera images to Gemini")
    g.add_argument("--push-to-talk", action="store_true",
                   help="push to talk (L1, screen tap or Enter) instead of hands-free")
    main(hooks, processing, parser=parser, setup=voice.setup)
