#!/usr/bin/env python3
"""Demo 07 - Fotomatón: pasarela del portátil.

    export GEMINI_API_KEY=...        # para los filtros con IA (Ghibli, Gaming, Cómic)
    demos/07-fotomaton/run.sh        # flashea el robot y arranca esto

Es la pasarela de la demo 01 (botones y vídeo, que aquí no se usan) más
fotomaton.py (recibe la foto y devuelve la imagen filtrada) y filtros.py.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "01-vr-cardboard", "relay"))
sys.path.insert(0, HERE)

import filtros  # noqa: E402
import fotomaton  # noqa: E402
import hooks  # noqa: E402
import processing  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 07: fotomatón con impresora térmica")
    parser.add_argument("--modelo-imagen", default=filtros.MODELO, help="modelo de Gemini para los filtros con IA")
    args, _ = parser.parse_known_args()
    filtros.MODELO = args.modelo_imagen
    main(hooks, processing, parser=parser, setup=fotomaton.setup)
