"""Photo booth filters and conversion for the thermal printer.

Two kinds of filter:
- AI (Ghibli, Gaming, Comic, Picasso): Gemini redraws the photo in that style. To add
  one, write its prompt in PROMPTS and its button in the firmware (FILTERS).
- Local (Original, Sketch, Pixel): OpenCV on the laptop, no internet needed.

The printer only prints black dots: to_thermal() scales the image to 384 dots
wide and "dithers" it (Floyd-Steinberg), which fakes the greys with dots.
"""
import os

import cv2
import numpy as np

WIDTH = 384                  # dots per line on the MX10 (48 mm at 8 dots/mm)
MAX_HEIGHT = 576             # 7 cm of paper at most
MODEL = "gemini-3.1-flash-image"

_FINAL = (" Keep the same people, faces, poses and composition. It will be printed on a small black-and-white"
          " thermal printer: use bold clean outlines, strong contrast and a light background. No text, no frame.")
PROMPTS = {
    "ghibli": "Redraw this photo as a still from a hand-drawn Studio Ghibli animated film, soft and whimsical." + _FINAL,
    "gaming": "Turn this photo into a scene from a retro 16-bit video game: pixel-art characters, a game HUD"
              " with a health bar and a score in the corners." + _FINAL,
    "comic": "Redraw this photo as a black-and-white comic book panel, with ink lines, halftone shading and"
             " dramatic action-hero style." + _FINAL,
    "picasso": "Repaint this photo as a Cubist portrait in the style of Pablo Picasso: faces and bodies broken"
               " into bold geometric planes, seen from several angles at once, with thick dark outlines." + _FINAL,
}
LOCAL = ("original", "sketch", "pixel")
NAMES = {"original": "Original", "ghibli": "Ghibli", "gaming": "Gaming", "comic": "Comic", "picasso": "Picasso",
         "sketch": "Sketch", "pixel": "Pixel"}


def apply(name, jpeg, status):
    """Returns the photo (BGR) with the filter. status(text) reports on the robot's screen."""
    img = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError("the photo arrived damaged")
    if name in PROMPTS:
        status(f"Gemini is drawing your {NAMES[name]} photo...")
        return _gemini(img, PROMPTS[name])
    if name == "sketch":
        gray, _ = cv2.pencilSketch(img, sigma_s=60, sigma_r=0.07, shade_factor=0.04)
        return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    if name == "pixel":
        return _pixel(img)
    if name == "original":
        return img
    raise ValueError(f"unknown filter {name}")


def _gemini(img, prompt):
    from google import genai
    from google.genai import types
    if not os.environ.get("GEMINI_API_KEY"):
        raise RuntimeError("GEMINI_API_KEY is missing on the laptop")
    ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 92])
    client = genai.Client()
    resp = client.models.generate_content(
        model=MODEL,
        contents=[types.Part.from_bytes(data=jpg.tobytes(), mime_type="image/jpeg"), prompt],
        config=types.GenerateContentConfig(response_modalities=["IMAGE", "TEXT"]))
    text = []
    for cand in resp.candidates or []:
        for part in (cand.content.parts if cand.content else []):
            if part.inline_data and part.inline_data.data:
                out = cv2.imdecode(np.frombuffer(part.inline_data.data, np.uint8), cv2.IMREAD_COLOR)
                if out is not None:
                    return out
            if part.text:
                text.append(part.text)
    raise RuntimeError("Gemini returned no image" + (f": {' '.join(text)[:80]}" if text else ""))


def _pixel(img):
    """Pixel art: dithered at 64 dots wide and scaled up without smoothing. Comes out black and white already."""
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    height = round(gray.shape[0] * 64 / gray.shape[1])
    small = _contrast(cv2.resize(gray, (64, height), interpolation=cv2.INTER_AREA))
    bw = np.where(_floyd_steinberg(small), 0, 255).astype(np.uint8)
    return cv2.cvtColor(cv2.resize(bw, (WIDTH, height * WIDTH // 64), interpolation=cv2.INTER_NEAREST), cv2.COLOR_GRAY2BGR)


def _contrast(gray):
    """Stretches the histogram and lightens a bit: thermal paper darkens the greys."""
    lo, hi = np.percentile(gray, (2, 98))
    g = np.clip((gray.astype(np.float32) - lo) / max(1.0, hi - lo), 0, 1)
    return (np.power(g, 0.8) * 255).astype(np.uint8)


def _floyd_steinberg(gray):
    """True where a black dot goes. Python loop over lists: ~0.3 s for 384x288."""
    h, w = gray.shape
    px = gray.astype(np.float32).tolist()
    black = np.zeros((h, w), bool)
    for y in range(h):
        row, below = px[y], px[y + 1] if y + 1 < h else None
        for x in range(w):
            v = row[x]
            new = 0.0 if v < 128 else 255.0
            if new == 0.0:
                black[y, x] = True
            err = v - new
            if x + 1 < w:
                row[x + 1] += err * 7 / 16
            if below is not None:
                if x > 0:
                    below[x - 1] += err * 3 / 16
                below[x] += err * 5 / 16
                if x + 1 < w:
                    below[x + 1] += err * 1 / 16
    return black


def to_thermal(img):
    """BGR image -> (packed rows for the MX10, height, grey preview).
    48 bytes per row, least significant bit first, 1 = black dot."""
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    height = round(gray.shape[0] * WIDTH / gray.shape[1])
    gray = cv2.resize(gray, (WIDTH, height), interpolation=cv2.INTER_AREA)
    if height > MAX_HEIGHT:                   # too tall: crop around the center
        y0 = (height - MAX_HEIGHT) // 2
        gray, height = gray[y0:y0 + MAX_HEIGHT], MAX_HEIGHT
    if np.isin(gray, (0, 255)).all():         # already black and white (Pixel): no dithering
        black = gray < 128
    else:
        black = _floyd_steinberg(_contrast(gray))
    rows = np.packbits(black, axis=1, bitorder="little").tobytes()
    return rows, height, np.where(black, 0, 255).astype(np.uint8)
