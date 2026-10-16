#!/usr/bin/env python3
"""Demo 01 - VR con Cardboard: pasarela del portátil.

    ./run_relay.sh [--show] [--enhance] [--record DIR] ...   (--help para ver todo)

La pasarela base está en lib/python/stackchan/relay.py; aquí solo se le pasan
los dos ficheros de esta demo que puedes editar: processing.py y hooks.py.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, HERE)

import hooks  # noqa: E402
import processing  # noqa: E402
from stackchan.relay import main  # noqa: E402

if __name__ == "__main__":
    main(hooks, processing)
