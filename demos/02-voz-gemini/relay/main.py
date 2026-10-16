#!/usr/bin/env python3
"""Demo 02 - Voz con Gemini: pasarela del portátil.

    export GEMINI_API_KEY=...        # https://aistudio.google.com/apikey
    ./run_relay.sh [--pulsar] [--modelo ...] [--voz Puck] [--sin-vision]

Es la pasarela de la demo 01 más voice.py (Gemini Live). Los filtros están en
processing.py y los botones en hooks.py.
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
    parser = build_parser("Demo 02: Stack-chan con voz (Gemini Live)")
    g = parser.add_argument_group("voz")
    g.add_argument("--modelo", default="gemini-3.1-flash-live-preview", help="modelo de Gemini Live")
    g.add_argument("--voz", default="Puck", help="voz de Gemini: Puck, Charon, Kore, Fenrir, Aoede...")
    g.add_argument("--sin-vision", action="store_true", help="no enviar imágenes de la cámara a Gemini")
    g.add_argument("--pulsar", action="store_true",
                   help="pulsar para hablar (L1, toque o Enter) en vez de manos libres")
    main(hooks, processing, parser=parser, setup=voice.setup)
