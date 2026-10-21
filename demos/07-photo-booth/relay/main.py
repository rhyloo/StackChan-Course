#!/usr/bin/env python3
"""Demo 07 - Photo booth: the laptop relay.

    export GEMINI_API_KEY=...          # for the AI filters (Ghibli, Gaming, Comic, Picasso)
    demos/07-photo-booth/run.sh        # flashes the robot and starts this

It is the demo 01 relay (buttons and video, not used here) plus
photo_booth.py (receives the photo and sends back the filtered image) and filters.py.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "01-vr-cardboard", "relay"))
sys.path.insert(0, HERE)

import filters  # noqa: E402
import hooks  # noqa: E402
import photo_booth  # noqa: E402
import processing  # noqa: E402
from stackchan.relay import build_parser, main  # noqa: E402

if __name__ == "__main__":
    parser = build_parser("Demo 07: photo booth with a thermal printer")
    parser.add_argument("--image-model", default=filters.MODEL, help="Gemini model for the AI filters")
    args, _ = parser.parse_known_args()
    filters.MODEL = args.image_model
    main(hooks, processing, parser=parser, setup=photo_booth.setup)
