#!/usr/bin/env python3
"""Demo 01 - Cardboard VR: laptop relay.

    ./server.sh [--show] [--enhance] [--record DIR] ...   (--help shows everything)

The base relay lives in lib/python/stackchan/relay.py; here we only hand it
the two files of this demo you can edit: processing.py and hooks.py.
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
