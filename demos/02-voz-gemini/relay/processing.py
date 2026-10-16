"""Filtros de imagen que se activan con la voz ("activa la visión nocturna").

Gemini llama a la función activar_filtro (voice.py) y esta cambia FILTRO.
Para añadir un filtro: escribe la función, añádela a FILTROS y Gemini ya
podrá usarla (la lista de nombres se le pasa automáticamente).
"""
import cv2
import numpy as np

FILTRO = "ninguno"

_clahe = cv2.createCLAHE(clipLimit=2.5, tileGridSize=(8, 8))
_faces = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")


def contraste(img):
    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    return cv2.cvtColor(cv2.merge((_clahe.apply(l), a, b)), cv2.COLOR_LAB2BGR)


def nocturna(img):
    g = _clahe.apply(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY))
    g = cv2.add(g, np.random.randint(0, 25, g.shape, np.uint8))   # grano
    out = np.zeros_like(img)
    out[:, :, 1] = g
    return out


def bordes(img):
    e = cv2.Canny(cv2.GaussianBlur(img, (5, 5), 0), 50, 120)
    out = img // 3
    out[e > 0] = (0, 255, 255)
    return out


def grises(img):
    return cv2.cvtColor(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), cv2.COLOR_GRAY2BGR)


def caricatura(img):
    color = cv2.bilateralFilter(img, 7, 60, 60)
    edges = cv2.adaptiveThreshold(cv2.medianBlur(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), 5), 255,
                                  cv2.ADAPTIVE_THRESH_MEAN_C, cv2.THRESH_BINARY, 9, 4)
    return cv2.bitwise_and(color, color, mask=edges)


def termica(img):
    return cv2.applyColorMap(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), cv2.COLORMAP_JET)


def caras(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    for (x, y, w, h) in _faces.detectMultiScale(gray, 1.2, 5, minSize=(30, 30)):
        cv2.rectangle(img, (x, y), (x + w, y + h), (0, 255, 0), 2)
    return img


FILTROS = {
    "ninguno": lambda img: img,
    "contraste": contraste,
    "nocturna": nocturna,
    "bordes": bordes,
    "grises": grises,
    "caricatura": caricatura,
    "termica": termica,
    "caras": caras,
}

ultimo_fotograma = None   # para que Gemini "vea" y para guardar fotos


def process(frame, options):
    global ultimo_fotograma
    ultimo_fotograma = frame.copy()
    return FILTROS.get(FILTRO, FILTROS["ninguno"])(frame)


def needs_decoding(options):
    return True   # los filtros pueden activarse en cualquier momento
