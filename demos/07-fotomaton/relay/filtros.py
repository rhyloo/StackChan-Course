"""Filtros del fotomatón y conversión a la impresora térmica.

Dos tipos de filtro:
- Con IA (Ghibli, Gaming, Cómic): Gemini redibuja la foto con ese estilo. Para
  añadir uno, escribe su prompt en PROMPTS y su botón en el firmware (FILTERS).
- Locales (Original, Boceto, Píxel): OpenCV en el portátil, sin internet.

La impresora solo pinta puntos negros: a_termica() pasa la imagen a 384 puntos
de ancho y la «trama» (Floyd-Steinberg), que simula los grises con puntos.
"""
import os

import cv2
import numpy as np

ANCHO = 384                  # puntos por línea de la MX10 (48 mm a 8 puntos/mm)
ALTO_MAX = 576               # 7 cm de papel como mucho
MODELO = "gemini-3.1-flash-image"

_FINAL = (" Keep the same people, faces, poses and composition. It will be printed on a small black-and-white"
          " thermal printer: use bold clean outlines, strong contrast and a light background. No text, no frame.")
PROMPTS = {
    "ghibli": "Redraw this photo as a still from a hand-drawn Studio Ghibli animated film, soft and whimsical." + _FINAL,
    "gaming": "Turn this photo into a scene from a retro 16-bit video game: pixel-art characters, a game HUD"
              " with a health bar and a score in the corners." + _FINAL,
    "comic": "Redraw this photo as a black-and-white comic book panel, with ink lines, halftone shading and"
             " dramatic action-hero style." + _FINAL,
}
LOCALES = ("original", "boceto", "pixel")
NOMBRES = {"original": "Original", "ghibli": "Ghibli", "gaming": "Gaming", "comic": "Cómic",
           "boceto": "Boceto", "pixel": "Píxel"}


def aplicar(nombre, jpeg, estado):
    """Devuelve la foto (BGR) con el filtro. estado(texto) informa en la pantalla del robot."""
    img = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError("la foto llegó dañada")
    if nombre in PROMPTS:
        estado(f"Gemini dibuja tu foto {NOMBRES[nombre]}...")
        return _gemini(img, PROMPTS[nombre])
    if nombre == "boceto":
        gris, _ = cv2.pencilSketch(img, sigma_s=60, sigma_r=0.07, shade_factor=0.04)
        return cv2.cvtColor(gris, cv2.COLOR_GRAY2BGR)
    if nombre == "pixel":
        return _pixel(img)
    if nombre == "original":
        return img
    raise ValueError(f"no conozco el filtro {nombre}")


def _gemini(img, prompt):
    from google import genai
    from google.genai import types
    if not os.environ.get("GEMINI_API_KEY"):
        raise RuntimeError("falta GEMINI_API_KEY en el portátil")
    ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 92])
    client = genai.Client()
    resp = client.models.generate_content(
        model=MODELO,
        contents=[types.Part.from_bytes(data=jpg.tobytes(), mime_type="image/jpeg"), prompt],
        config=types.GenerateContentConfig(response_modalities=["IMAGE", "TEXT"]))
    texto = []
    for cand in resp.candidates or []:
        for part in (cand.content.parts if cand.content else []):
            if part.inline_data and part.inline_data.data:
                out = cv2.imdecode(np.frombuffer(part.inline_data.data, np.uint8), cv2.IMREAD_COLOR)
                if out is not None:
                    return out
            if part.text:
                texto.append(part.text)
    raise RuntimeError("Gemini no ha devuelto imagen" + (f": {' '.join(texto)[:80]}" if texto else ""))


def _pixel(img):
    """Pixel art: tramado a 64 puntos de ancho y ampliado sin suavizar. Sale ya en blanco y negro."""
    gris = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    alto = round(gris.shape[0] * 64 / gris.shape[1])
    peque = _contraste(cv2.resize(gris, (64, alto), interpolation=cv2.INTER_AREA))
    bn = np.where(_floyd_steinberg(peque), 0, 255).astype(np.uint8)
    return cv2.cvtColor(cv2.resize(bn, (ANCHO, alto * ANCHO // 64), interpolation=cv2.INTER_NEAREST), cv2.COLOR_GRAY2BGR)


def _contraste(gris):
    """Estira el histograma y aclara un poco: el papel térmico oscurece los grises."""
    lo, hi = np.percentile(gris, (2, 98))
    g = np.clip((gris.astype(np.float32) - lo) / max(1.0, hi - lo), 0, 1)
    return (np.power(g, 0.8) * 255).astype(np.uint8)


def _floyd_steinberg(gris):
    """True donde va un punto negro. Bucle en Python sobre listas: ~0,3 s para 384x288."""
    h, w = gris.shape
    px = gris.astype(np.float32).tolist()
    negro = np.zeros((h, w), bool)
    for y in range(h):
        fila, sig = px[y], px[y + 1] if y + 1 < h else None
        for x in range(w):
            v = fila[x]
            nuevo = 0.0 if v < 128 else 255.0
            if nuevo == 0.0:
                negro[y, x] = True
            err = v - nuevo
            if x + 1 < w:
                fila[x + 1] += err * 7 / 16
            if sig is not None:
                if x > 0:
                    sig[x - 1] += err * 3 / 16
                sig[x] += err * 5 / 16
                if x + 1 < w:
                    sig[x + 1] += err * 1 / 16
    return negro


def a_termica(img):
    """Imagen BGR -> (filas empaquetadas para la MX10, alto, vista previa en gris).
    48 bytes por fila, el bit menos significativo primero, 1 = punto negro."""
    gris = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    alto = round(gris.shape[0] * ANCHO / gris.shape[1])
    gris = cv2.resize(gris, (ANCHO, alto), interpolation=cv2.INTER_AREA)
    if alto > ALTO_MAX:                       # muy alta: recorta por el centro
        y0 = (alto - ALTO_MAX) // 2
        gris, alto = gris[y0:y0 + ALTO_MAX], ALTO_MAX
    if np.isin(gris, (0, 255)).all():         # ya es blanco y negro (Píxel): sin tramar
        negro = gris < 128
    else:
        negro = _floyd_steinberg(_contraste(gris))
    filas = np.packbits(negro, axis=1, bitorder="little").tobytes()
    return filas, alto, np.where(negro, 0, 255).astype(np.uint8)
