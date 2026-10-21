#!/usr/bin/env python3
"""Demo 05 - Write NFC cards with your voice: the laptop relay.

    export GEMINI_API_KEY=...        # https://aistudio.google.com/apikey
    demos/05-nfc-voice/run.sh          # flashes the robot and starts this

It is the demo 02 relay (voice with Gemini Live) with the functions in
nfc_voice.py. The buttons (hooks.py) and the filters (processing.py) are the
ones from demo 02.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "02-gemini-voice", "relay"))
sys.path.insert(0, HERE)

import hooks  # noqa: E402
import nfc_voice  # noqa: E402
import processing  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 05: write NFC cards with your voice (Gemini Live)")
    g = parser.add_argument_group("voice")
    g.add_argument("--model", default="gemini-3.1-flash-live-preview", help="Gemini Live model")
    g.add_argument("--voice", default="Puck", help="Gemini voice: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--push-to-talk", action="store_true",
                   help="push to talk (L1, screen tap or Enter) instead of hands-free")
    parser.set_defaults(no_vision=True)   # this robot sends no video
    main(hooks, processing, parser=parser, setup=nfc_voice.setup)
