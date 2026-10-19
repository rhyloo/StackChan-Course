#!/usr/bin/env python3
"""Demo 05 - Escribir tarjetas NFC con la voz: pasarela del portátil.

    export GEMINI_API_KEY=...        # https://aistudio.google.com/apikey
    demos/05-nfc-voz/run.sh          # flashea el robot y arranca esto

Es la pasarela de la demo 02 (voz con Gemini Live) con las funciones de
nfc_voice.py. Los botones (hooks.py) y los filtros (processing.py) son los de
la demo 02.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "02-voz-gemini", "relay"))
sys.path.insert(0, HERE)

import hooks  # noqa: E402
import nfc_voice  # noqa: E402
import processing  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 05: escribir tarjetas NFC con la voz (Gemini Live)")
    g = parser.add_argument_group("voz")
    g.add_argument("--modelo", default="gemini-3.1-flash-live-preview", help="modelo de Gemini Live")
    g.add_argument("--voz", default="Puck", help="voz de Gemini: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--pulsar", action="store_true",
                   help="pulsar para hablar (L1, toque o Enter) en vez de manos libres")
    parser.set_defaults(sin_vision=True)   # este robot no manda vídeo
    main(hooks, processing, parser=parser, setup=nfc_voice.setup)
